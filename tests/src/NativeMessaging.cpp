// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/Args.hpp"
#include "common/Modes.hpp"
#include "singletons/NativeMessaging.hpp"
#include "singletons/Paths.hpp"
#include "Test.hpp"
#include "util/CombinePath.hpp"
#include "util/IpcQueue.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <chrono>


using namespace chatterino;

using namespace chatterino::nm::detail;

TEST(AttachmentSessionRegistry, RejectsStaleAndMakesDetachIdempotent)
{
    AttachmentSessionRegistry registry;
    AttachmentSession first{.sessionId = "a",
                            .browserWindowId = "window-a",
                            .tabId = 1,
                            .generation = 4,
                            .channel = "example",
                            .browserHwnd = 100,
                            .leaseExpiresAt = 1000};
    EXPECT_EQ(registry.prepare(first),
              AttachmentSessionRegistry::PrepareResult::Prepared);
    EXPECT_EQ(registry.prepare(first),
              AttachmentSessionRegistry::PrepareResult::AlreadyCurrent);

    auto stale = first;
    stale.generation = 3;
    EXPECT_EQ(registry.prepare(stale),
              AttachmentSessionRegistry::PrepareResult::Stale);
    EXPECT_TRUE(registry.remove(first.sessionId, first.generation));
    EXPECT_FALSE(registry.remove(first.sessionId, first.generation));
}

TEST(AttachmentSessionRegistry, DoesNotChooseBetweenSameChannelWindows)
{
    AttachmentSessionRegistry registry;
    AttachmentSession first{.sessionId = "a",
                            .browserWindowId = "window-a",
                            .tabId = 1,
                            .generation = 1,
                            .channel = "example",
                            .browserHwnd = 100,
                            .leaseExpiresAt = 1000};
    auto second = first;
    second.sessionId = "b";
    second.browserWindowId = "window-b";
    second.tabId = 2;
    second.browserHwnd = 200;
    ASSERT_TRUE(registry.markReady(first.sessionId, first.generation) == false);
    ASSERT_EQ(registry.prepare(first),
              AttachmentSessionRegistry::PrepareResult::Prepared);
    ASSERT_EQ(registry.prepare(second),
              AttachmentSessionRegistry::PrepareResult::Prepared);
    ASSERT_TRUE(registry.markReady(first.sessionId, first.generation));
    ASSERT_TRUE(registry.markReady(second.sessionId, second.generation));
    EXPECT_FALSE(registry.uniqueReadyForChannel("example").has_value());
}

TEST(AttachmentSessionRegistry, ExpiresOnlyOverdueSessions)
{
    AttachmentSessionRegistry registry;
    AttachmentSession expired{.sessionId = "expired",
                              .browserWindowId = "window-a",
                              .tabId = 1,
                              .generation = 1,
                              .channel = "example",
                              .browserHwnd = 100,
                              .leaseExpiresAt = 100};
    ASSERT_EQ(registry.prepare(expired),
              AttachmentSessionRegistry::PrepareResult::Prepared);
    const auto removed = registry.expire(100);
    ASSERT_EQ(removed.size(), 1);
    EXPECT_EQ(removed.front().sessionId, "expired");
    EXPECT_TRUE(registry.empty());
}

TEST(AttachmentSessionRegistry, ResultIdentityMustMatchEverySessionField)
{
    AttachmentSessionRegistry registry;
    AttachmentSession session{
        .sessionId = "opaque",
        .browserWindowId = "window-a",
        .tabId = 7,
        .generation = 4,
        .channel = "example",
        .browserHwnd = 100,
        .leaseExpiresAt = QDateTime::currentMSecsSinceEpoch() + 10000};
    ASSERT_EQ(registry.prepare(session),
              AttachmentSessionRegistry::PrepareResult::Prepared);
    ASSERT_TRUE(registry.markReady(session.sessionId, session.generation));
    EXPECT_TRUE(registry.matchesReadyIdentity(session));

    auto wrongTab = session;
    wrongTab.tabId++;
    EXPECT_FALSE(registry.matchesReadyIdentity(wrongTab));
    auto wrongChannel = session;
    wrongChannel.channel = "other";
    EXPECT_FALSE(registry.matchesReadyIdentity(wrongChannel));
}

TEST(IpcQueue, EmptyPayloadHasDefinitiveDeliveryStatus)
{
    EXPECT_EQ(ipc::sendMessage("chatterino-test-no-queue", QByteArray{}),
              ipc::DeliveryStatus::InvalidMessage);
}

TEST(IpcQueue, SendToAbsentQueueFailsFastInsteadOfHanging)
{
    // A file-backed queue whose emulated interprocess mutex was poisoned by
    // a force-killed peer used to spin forever inside try_send. The bounded
    // timed_send must instead return a definitive status promptly.
    const auto start = std::chrono::steady_clock::now();
    const auto status = ipc::sendMessage(
        "chatterino-test-absent-queue",
        QByteArrayLiteral("{\"action\":\"sync\"}"));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(status, ipc::DeliveryStatus::QueueUnavailable);
    // The bound is 250ms; allow generous slack for slow CI machines while
    // still failing if the call were to hang indefinitely.
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(IpcQueue, ReplacedQueueAcceptsNewMessages)
{
    // The GUI-side recovery path: after tryReplaceOrCreate, sends must
    // deliver again even when a stale file existed before.
    using namespace std::chrono_literals;

    // The queue subsystem resolves its shared directory through Paths.
    Args args;
    Modes modes{args};
    Paths paths{args, modes};
    ipc::initPaths(&paths);

    const char *queueName = "chatterino-test-replace-queue";
    ipc::IpcQueue::remove(queueName);

    auto [first, firstError] =
        ipc::IpcQueue::tryReplaceOrCreate(queueName, 10, 1024);
    ASSERT_TRUE(firstError.isEmpty());
    // Abandon the first queue without draining it, simulating a crashed
    // previous owner leaving stale state behind.
    first.reset();

    auto [second, secondError] =
        ipc::IpcQueue::tryReplaceOrCreate(queueName, 10, 1024);
    ASSERT_TRUE(secondError.isEmpty());

    EXPECT_EQ(ipc::sendMessage(queueName,
                               QByteArrayLiteral("{\"action\":\"sync\"}")),
              ipc::DeliveryStatus::Delivered);

    const auto received = second->receiveFor(1s);
    EXPECT_EQ(received, QByteArrayLiteral("{\"action\":\"sync\"}"));
}

class NativeMessagingFixture : public ::testing::Test
{
    QTemporaryDir tempDir;

protected:
    NativeMessagingFixture()
        : dir(combinePath(this->tempDir.path(), QString("native-messaging")))
    {
    }

    QDir dir;
};

TEST_F(NativeMessagingFixture, writeManifestToSubDirCreated)
{
    // writeManifestTo should succeed if the subdir directory is created
    ASSERT_TRUE(this->dir.mkpath("."));

    ASSERT_TRUE(writeManifestTo(this->dir.path(), "native-messaging-hosts",
                                "test.json", QJsonDocument())
                    .has_value());

    QDir nmDir = combinePath(this->dir.path(), "native-messaging-hosts");

    ASSERT_TRUE(nmDir.exists());

    ASSERT_TRUE(QFile(combinePath(nmDir.path(), "test.json")).exists());
}

TEST_F(NativeMessagingFixture, writeManifestToSubDirNotCreated)
{
    // writeManifestTo should fail if the subdir is not created
    ASSERT_EQ(writeManifestTo(this->dir.path(), "native-messaging-hosts",
                              "test.json", QJsonDocument())
                  .error(),
              WriteManifestError::FailedToCreateDirectory);
}

TEST_F(NativeMessagingFixture, writeManifestToWindowsCurrentDir)
{
    // writeManifestTo should succeed if the subdir is created and the path to create is "."
    ASSERT_TRUE(this->dir.mkpath("."));

    ASSERT_TRUE(
        writeManifestTo(this->dir.path(), ".", "test.json", QJsonDocument())
            .has_value());

    ASSERT_TRUE(QFile(combinePath(this->dir.path(), "test.json")).exists());
}

#ifndef Q_OS_WIN
TEST(NativeMessaging, parseCustomPath)
{
    ASSERT_EQ(parseCustomPath("/my/custom/path/to/manifest.json"),
              std::optional{"/my/custom/path/to/manifest.json"});

    ASSERT_EQ(parseCustomPath(""), std::nullopt);

    ASSERT_EQ(parseCustomPath("~/path/to/manifest.json"),
              std::optional{QDir::homePath() % "/path/to/manifest.json"});

    ASSERT_EQ(parseCustomPath("relative/path/to/manifest.json"), std::nullopt);

#    ifdef Q_OS_LINUX
    ASSERT_EQ(parseCustomPath("$XDG_CONFIG_HOME/path/to/manifest.json"),
              std::optional{QStandardPaths::standardLocations(
                                QStandardPaths::GenericConfigLocation)
                                .constFirst() %
                            "/path/to/manifest.json"});

    ASSERT_EQ(parseCustomPath("$XDG_DATA_HOME/path/to/manifest.json"),
              std::optional{QStandardPaths::standardLocations(
                                QStandardPaths::GenericDataLocation)
                                .constFirst() %
                            "/path/to/manifest.json"});
#    endif
}
#endif
