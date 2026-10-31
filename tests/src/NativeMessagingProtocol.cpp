// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/NativeMessagingProtocol.hpp"

#include <gtest/gtest.h>

using chatterino::nm::fitsIpcQueue;
using chatterino::nm::IPC_QUEUE_MAX_MESSAGES;
using chatterino::nm::isValidBrowserChannelName;
using chatterino::nm::isValidNativeMessageSize;
using chatterino::nm::MAX_ENGAGEMENT_WINDOW_MS;
using chatterino::nm::MAX_IPC_MESSAGE_SIZE;
using chatterino::nm::MAX_NATIVE_MESSAGE_SIZE;
using chatterino::nm::NativeAction;
using chatterino::nm::parseEngagementClosesAt;
using chatterino::nm::parseNativeMessage;

TEST(NativeMessagingProtocol, LegacyAndCurrentVersionsAreAccepted)
{
    EXPECT_EQ(parseNativeMessage({{"action", "prediction"}}).action,
              NativeAction::PredictionLegacy);
    EXPECT_EQ(
        parseNativeMessage({{"protocolVersion", 1}, {"action", "sync"}}).action,
        NativeAction::Sync);
}

TEST(NativeMessagingProtocol, FutureVersionIsRejected)
{
    const auto parsed =
        parseNativeMessage({{"protocolVersion", 3}, {"action", "sync"}});
    EXPECT_FALSE(parsed);
    EXPECT_FALSE(parsed.error.isEmpty());
}

TEST(NativeMessagingProtocol, NativeFrameSizeIsBoundedBeforeAllocation)
{
    EXPECT_FALSE(isValidNativeMessageSize(0));
    EXPECT_TRUE(isValidNativeMessageSize(MAX_NATIVE_MESSAGE_SIZE));
    EXPECT_FALSE(isValidNativeMessageSize(MAX_NATIVE_MESSAGE_SIZE + 1));
}

TEST(NativeMessagingProtocol, IpcQueueLimitCoversRealFramesAndStaysBounded)
{
    // The old 1 KiB queue silently dropped long poll/prediction frames.
    EXPECT_TRUE(fitsIpcQueue(4096));
    EXPECT_TRUE(fitsIpcQueue(MAX_IPC_MESSAGE_SIZE));
    EXPECT_FALSE(fitsIpcQueue(MAX_IPC_MESSAGE_SIZE + 1ULL));
    EXPECT_FALSE(fitsIpcQueue(0));
    // Anything the IPC queue accepts is also a valid browser frame.
    EXPECT_TRUE(isValidNativeMessageSize(MAX_IPC_MESSAGE_SIZE));
    // A queue reserves maxMessages * maxMessageSize bytes up front.
    EXPECT_LE(std::uint64_t{IPC_QUEUE_MAX_MESSAGES} * MAX_IPC_MESSAGE_SIZE,
              4ULL * 1024 * 1024);
}

TEST(NativeMessagingProtocol, V2SelectRequiresCompleteIdentityAndGeometry)
{
    const QJsonObject valid{
        {"protocolVersion", 2},
        {"action", "select"},
        {"sessionId", "opaque-session"},
        {"browserWindowId", "window"},
        {"tabId", 4},
        {"generation", 2},
        {"name", "example"},
        {"browserHwnd", "1234"},
        {"leaseExpiresAt", 12345},
        {"size", QJsonObject{{"width", 320}, {"height", 480}}},
    };
    EXPECT_TRUE(parseNativeMessage(valid));

    auto invalidGeometry = valid;
    invalidGeometry["size"] = QJsonObject{{"width", 0}, {"height", 480}};
    EXPECT_FALSE(parseNativeMessage(invalidGeometry));

    auto incomplete = valid;
    incomplete.remove("sessionId");
    EXPECT_FALSE(parseNativeMessage(incomplete));

    auto coercibleIdentity = valid;
    coercibleIdentity["tabId"] = "4";
    EXPECT_FALSE(parseNativeMessage(coercibleIdentity));

    auto oversizedGeometry = valid;
    oversizedGeometry["size"] = QJsonObject{{"width", 32769}, {"height", 480}};
    EXPECT_FALSE(parseNativeMessage(oversizedGeometry));
}

TEST(NativeMessagingProtocol, V2SelectAllowsOfficialForegroundWindowFallback)
{
    const QJsonObject select{
        {"protocolVersion", 2},
        {"action", "select"},
        {"sessionId", "opaque-session"},
        {"browserWindowId", "window"},
        {"tabId", 4},
        {"generation", 2},
        {"name", "example"},
        {"leaseExpiresAt", 12345},
        {"size", QJsonObject{{"width", 320}, {"height", 480}}},
    };

    // Chromium/Edge may launch native messaging hosts with
    // --parent-window=0. Original Chatterino attaches through the foreground
    // browser window, so the v2 envelope must not reject this valid select
    // before the handler can use the same fallback.
    EXPECT_TRUE(parseNativeMessage(select));
}

TEST(NativeMessagingProtocol, ExtremeNumericVersionIsRejectedBeforeCast)
{
    EXPECT_FALSE(
        parseNativeMessage({{"protocolVersion", 1e100}, {"action", "sync"}}));
}

TEST(NativeMessagingProtocol, V2LeaseRequiresExpiry)
{
    QJsonObject lease{{"protocolVersion", 2},
                      {"action", "leaseRenew"},
                      {"sessionId", "opaque-session"},
                      {"browserWindowId", "window"},
                      {"tabId", 4},
                      {"generation", 2},
                      {"channel", "example"}};
    EXPECT_FALSE(parseNativeMessage(lease));
    lease["leaseExpiresAt"] = 12345;
    EXPECT_TRUE(parseNativeMessage(lease));
}

TEST(NativeMessagingProtocol, V2NativeChatResultIsCorrelated)
{
    QJsonObject result{{"protocolVersion", 2},
                       {"action", "nativeChatResult"},
                       {"sessionId", "opaque-session"},
                       {"browserWindowId", "window"},
                       {"tabId", 4},
                       {"generation", 2},
                       {"channel", "example"},
                       {"requestId", "request"},
                       {"status", "uncertain"}};
    EXPECT_TRUE(parseNativeMessage(result));
    result["status"] = "sent";
    EXPECT_FALSE(parseNativeMessage(result));
}

TEST(NativeMessagingProtocol, FractionalVersionIsRejected)
{
    EXPECT_FALSE(
        parseNativeMessage({{"protocolVersion", 1.5}, {"action", "sync"}}));
}

TEST(NativeMessagingProtocol, CurrentEngagementShapeIsValidated)
{
    EXPECT_TRUE(parseNativeMessage({
        {"protocolVersion", 1},
        {"action", "engagement"},
        {"lifecycle", "remove"},
        {"kind", "poll"},
        {"channel", "example"},
    }));
    EXPECT_FALSE(parseNativeMessage({
        {"protocolVersion", 1},
        {"action", "engagement"},
        {"lifecycle", "upsert"},
        {"kind", "prediction"},
        {"channel", "example"},
    }));
}

TEST(NativeMessagingProtocol, EngagementChannelMustBeTwitchLogin)
{
    EXPECT_TRUE(isValidBrowserChannelName(QStringLiteral("forsen")));
    EXPECT_TRUE(isValidBrowserChannelName(QStringLiteral("#Forsen")));
    EXPECT_FALSE(isValidBrowserChannelName(QStringLiteral("/whispers")));
    EXPECT_FALSE(isValidBrowserChannelName(QStringLiteral("/mentions")));
    EXPECT_FALSE(isValidBrowserChannelName(QStringLiteral("_leading")));
    EXPECT_FALSE(isValidBrowserChannelName(QStringLiteral("a b")));
    EXPECT_FALSE(isValidBrowserChannelName(
        QStringLiteral("abcdefghijklmnopqrstuvwxyz")));

    EXPECT_FALSE(parseNativeMessage({
        {"protocolVersion", 1},
        {"action", "engagement"},
        {"lifecycle", "remove"},
        {"kind", "poll"},
        {"channel", "/whispers"},
    }));
}

TEST(NativeMessagingProtocol, EngagementDeadlineIsBounded)
{
    const qint64 now = 1700000000000;
    EXPECT_EQ(parseEngagementClosesAt(QJsonValue(double(now + 5000)), now),
              now + 5000);
    EXPECT_EQ(parseEngagementClosesAt(
                  QJsonValue(QStringLiteral("1700000005000")), now),
              now + 5000);
    EXPECT_EQ(parseEngagementClosesAt(QJsonValue(-1.0), now), 0);
    EXPECT_EQ(parseEngagementClosesAt(QJsonValue(1e300), now), 0);
    EXPECT_EQ(parseEngagementClosesAt(
                  QJsonValue(double(now + MAX_ENGAGEMENT_WINDOW_MS + 1)), now),
              0);
    EXPECT_EQ(parseEngagementClosesAt(QJsonValue(QStringLiteral("nan")), now),
              0);
    EXPECT_EQ(parseEngagementClosesAt(QJsonValue(), now), 0);
}
