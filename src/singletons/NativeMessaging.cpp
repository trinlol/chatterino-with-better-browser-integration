// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/NativeMessaging.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "common/Modes.hpp"
#include "common/QLogging.hpp"
#ifdef CHATTERINO_HAVE_PLUGINS
#    include "controllers/plugins/api/BetterBrowserEvent.hpp"
#    include "controllers/plugins/PluginController.hpp"
#endif
#include "debug/AssertInGuiThread.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/NativeMessagingProtocol.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/IpcQueue.hpp"
#include "util/PostToThread.hpp"
#include "util/XDGDirectory.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutexLocker>
#include <QSettings>
#include <QStringBuilder>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <syncstream>
#include <thread>

#ifdef Q_OS_WIN
#    include "widgets/AttachedWindow.hpp"

#    include <Windows.h>
#endif

namespace {

using namespace chatterino::nm::detail;
using namespace chatterino;
using namespace chatterino::literals;

const QString EXTENSION_ID = u"glknmaideaikkmemifbfkhnomoknepka"_s;
constexpr const size_t MESSAGE_SIZE = 1024;

struct Config {
#ifdef Q_OS_WIN
    QString fileName;
    QString registryKey;
#else
    QString browserDirectory;
    QString nmDirectory;
#endif
};

const Config FIREFOX{
#ifdef Q_OS_WIN
    .fileName = u"native-messaging-manifest-firefox.json"_s,
    .registryKey =
        u"HKCU\\Software\\Mozilla\\NativeMessagingHosts\\com.chatterino.chatterino"_s,
#elif defined(Q_OS_MACOS)
    .browserDirectory = u"~/Library/Application Support/Mozilla"_s,
    .nmDirectory = u"NativeMessagingHosts"_s,
#else
    .browserDirectory = u"~/.mozilla"_s,
    .nmDirectory = u"native-messaging-hosts"_s,
#endif
};

const Config CHROME{
#ifdef Q_OS_WIN
    .fileName = u"native-messaging-manifest-chrome.json"_s,
    .registryKey =
        u"HKCU\\Software\\Google\\Chrome\\NativeMessagingHosts\\com.chatterino.chatterino"_s,
#elif defined(Q_OS_MACOS)
    .browserDirectory = u"~/Library/Application Support/Google/Chrome/"_s,
    .nmDirectory = u"NativeMessagingHosts"_s,
#else
    .browserDirectory = u"~/.config/google-chrome"_s,
    .nmDirectory = u"NativeMessagingHosts"_s,
#endif
};

const Config EDGE{
#ifdef Q_OS_WIN
    .fileName = u"native-messaging-manifest-edge.json"_s,
    .registryKey =
        u"HKCU\\Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.chatterino.chatterino"_s,
#elif defined(Q_OS_MACOS)
    .browserDirectory = u"~/Library/Application Support/Microsoft Edge/"_s,
    .nmDirectory = u"NativeMessagingHosts"_s,
#else
    .browserDirectory = u"~/.config/microsoft-edge"_s,
    .nmDirectory = u"NativeMessagingHosts"_s,
#endif
};

#ifdef CHATTERINO_HAVE_PLUGINS
void dispatchBetterBrowserEvent(BetterBrowserEvent event)
{
    postToThread([event = std::move(event)] {
        if (auto *plugins = getApp()->getPlugins())
        {
            plugins->dispatchBetterBrowserEvent(event);
        }
    });
}
#endif

#ifdef Q_OS_WIN
bool isSupportedOverlayTarget(HWND window)
{
    if (window == nullptr || ::IsWindow(window) == 0)
    {
        return false;
    }
    DWORD processId = 0;
    if (::GetWindowThreadProcessId(window, &processId) == 0 || processId == 0)
    {
        return false;
    }
    const auto process =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, processId);
    if (process == nullptr)
    {
        return false;
    }
    wchar_t path[1024]{};
    DWORD pathLength = 1024;
    const bool havePath =
        ::QueryFullProcessImageNameW(process, 0, path, &pathLength) != 0;
    ::CloseHandle(process);
    if (!havePath)
    {
        return false;
    }
    const auto executable =
        QFileInfo(
            QString::fromWCharArray(path, static_cast<qsizetype>(pathLength)))
            .fileName();
    return executable.compare(u"chrome.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare(u"firefox.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare(u"vivaldi.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare(u"opera.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare(u"msedge.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare(u"brave.exe", Qt::CaseInsensitive) == 0;
}
#endif

void registerNmManifest([[maybe_unused]] const Paths &paths,
                        const Config &config, const QJsonDocument &document)
{
#ifdef Q_OS_WIN
    std::ignore =
        writeManifestTo(paths.miscDirectory, u"."_s, config.fileName, document);

    QSettings registry(config.registryKey, QSettings::NativeFormat);
    registry.setValue("",
                      QString(paths.miscDirectory % u'/' % config.fileName));
#else
    std::ignore =
        writeManifestTo(config.browserDirectory, config.nmDirectory,
                        u"com.chatterino.chatterino.json"_s, document);
#endif
}

QJsonObject buildBaseDocument()
{
    return QJsonObject{
        {u"name"_s, "com.chatterino.chatterino"_L1},
        {u"description"_s, "Browser interaction with chatterino."_L1},
        {u"path"_s, QCoreApplication::applicationFilePath()},
        {u"type"_s, "stdio"_L1},
    };
}

QJsonDocument buildChromeManifest(const QStringList &extensionIDs)
{
    auto obj = buildBaseDocument();
    QJsonArray allowedOriginsArr = {
        u"chrome-extension://%1/"_s.arg(EXTENSION_ID),
        u"chrome-extension://bogfpdfoagkaebimmlcbgmfmanhbhhlm/"_s,
        u"chrome-extension://ihmcbbdgnogenblkicmkhbmgglcepmfp/"_s};

    for (const auto &id : extensionIDs)
    {
        QString trimmedID = id.trimmed();
        if (!trimmedID.isEmpty())
        {
            allowedOriginsArr.append(
                u"chrome-extension://%1/"_s.arg(trimmedID));
        }
    }

    obj.insert("allowed_origins", allowedOriginsArr);

    return QJsonDocument{obj};
}

QJsonDocument buildFirefoxManifest(const QStringList &extensionIDs)
{
    auto obj = buildBaseDocument();
    QJsonArray allowedExtensions = {"chatterino_native@chatterino.com"};

    for (const auto &id : extensionIDs)
    {
        QString trimmedID = id.trimmed();
        if (!trimmedID.isEmpty())
        {
            allowedExtensions.append(trimmedID);
        }
    }

    obj.insert("allowed_extensions", allowedExtensions);

    return QJsonDocument{obj};
}

#ifndef Q_OS_WIN
void writeManifestToCustomPath(const QJsonDocument &manifest)
{
    auto customPath = parseCustomPath(
        getSettings()->customNativeMessagingManifestPath.getValue());
    if (!customPath.has_value())
    {
        return;
    }

    QFile file(customPath.value());
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        qCWarning(chatterinoNativeMessage)
            << "Failed to open" << customPath.value();
    }
    else
    {
        file.write(manifest.toJson());
    }
}
#endif

}  // namespace

namespace chatterino::nm::detail {

Expected<void, WriteManifestError> writeManifestTo(QString directory,
                                                   const QString &nmDirectory,
                                                   const QString &filename,
                                                   const QJsonDocument &json)
{
    if (directory.startsWith('~'))
    {
        directory = QDir::homePath() % QStringView{directory}.sliced(1);
    }

    QDir dir(directory);
    if (!dir.exists(nmDirectory) && !dir.mkdir(nmDirectory))
    {
        qCWarning(chatterinoNativeMessage)
            << "Failed to create" << nmDirectory << "in" << directory;
        return makeUnexpected(WriteManifestError::FailedToCreateDirectory);
    }
    dir.cd(nmDirectory);

    QFile file(dir.filePath(filename));
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        qCWarning(chatterinoNativeMessage)
            << "Failed to open" << filename << "in" << directory;
        return makeUnexpected(WriteManifestError::FailedToCreateFile);
    }
    file.write(json.toJson());

    return {};
}

#ifndef Q_OS_WIN
std::optional<QString> parseCustomPath(QString path)
{
    if (path.isEmpty())
    {
        return {};
    }

#    ifdef Q_OS_LINUX
    path = path.replace("$XDG_CONFIG_HOME",
                        getXDGUserDirectories(XDGDirectoryType::Config).at(0))
               .replace("$XDG_DATA_HOME",
                        getXDGUserDirectories(XDGDirectoryType::Data).at(0));
#    endif

    if (path.startsWith('~'))
    {
        path = QDir::homePath() % QStringView{path}.sliced(1);
    }

    if (!path.startsWith('/'))
    {
        return {};
    }

    return path;
}
#endif

}  // namespace chatterino::nm::detail

namespace chatterino {

using namespace chatterino::nm::detail;
using namespace literals;

void registerNmHost(const Modes &modes, const Paths &paths)
{
    if (modes.isPortable)
    {
        return;
    }

    QStringList extensionIDs =
        getSettings()->additionalExtensionIDs.getValue().split(
            ';', Qt::SkipEmptyParts);

    QJsonDocument chromeManifest = buildChromeManifest(extensionIDs);
    QJsonDocument firefoxManifest = buildFirefoxManifest(extensionIDs);

    registerNmManifest(paths, CHROME, chromeManifest);
    registerNmManifest(paths, EDGE, chromeManifest);
    registerNmManifest(paths, FIREFOX, firefoxManifest);

#ifndef Q_OS_WIN
    switch (getSettings()->customNativeMessagingManifestFormat.getEnum())
    {
        case BrowserManifestFormat::Chrome:
            writeManifestToCustomPath(chromeManifest);
            break;
        case BrowserManifestFormat::Firefox:
            writeManifestToCustomPath(firefoxManifest);
            break;
    }
#endif
}

std::string &getNmQueueName(const Paths &paths)
{
    static std::string name =
        "chatterino_gui" + paths.applicationFilePathHash.toStdString();
    return name;
}

const char *BROWSER_IPC_QUEUE_NAME = "chatterino_browser";

NativeMessagingServer *NativeMessagingServer::instance_ = nullptr;

namespace {

qint64 currentUtcMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

qint64 leaseExpiry(const QJsonObject &root)
{
    const auto explicitExpiry =
        root["leaseExpiresAt"_L1].toVariant().toLongLong();
    if (explicitExpiry > 0)
    {
        return explicitExpiry;
    }
    const auto duration = root["leaseDurationMs"_L1].toVariant().toLongLong();
    const auto legacyDuration =
        duration > 0 ? duration : root["leaseMs"_L1].toVariant().toLongLong();
    return legacyDuration > 0 ? currentUtcMs() + legacyDuration : 0;
}

bool sameIdentity(const AttachmentSession &left, const AttachmentSession &right)
{
    return left.sessionId == right.sessionId &&
           left.browserWindowId == right.browserWindowId &&
           left.tabId == right.tabId && left.generation == right.generation &&
           left.channel.compare(right.channel, Qt::CaseInsensitive) == 0 &&
           left.browserHwnd == right.browserHwnd;
}

}  // namespace

AttachmentSessionRegistry::PrepareResult AttachmentSessionRegistry::prepare(
    const AttachmentSession &candidate,
    std::optional<AttachmentSession> *replaced)
{
    QMutexLocker lock(&this->mutex_);
    const auto it = this->sessions_.find(candidate.sessionId);
    if (it == this->sessions_.end())
    {
        this->sessions_.insert(candidate.sessionId, candidate);
        return PrepareResult::Prepared;
    }
    if (candidate.generation < it->generation)
    {
        return PrepareResult::Stale;
    }
    if (candidate.generation == it->generation)
    {
        if (!sameIdentity(*it, candidate))
        {
            return PrepareResult::Conflict;
        }
        it->leaseExpiresAt =
            std::max(it->leaseExpiresAt, candidate.leaseExpiresAt);
        return PrepareResult::AlreadyCurrent;
    }

    if (replaced != nullptr)
    {
        *replaced = *it;
    }
    this->sessions_.insert(candidate.sessionId, candidate);
    return PrepareResult::Prepared;
}

AttachmentSessionRegistry::PrepareResult AttachmentSessionRegistry::renewLease(
    const AttachmentSession &candidate)
{
    QMutexLocker lock(&this->mutex_);
    const auto it = this->sessions_.find(candidate.sessionId);
    if (it == this->sessions_.end() || candidate.generation > it->generation)
    {
        return PrepareResult::Conflict;
    }
    if (candidate.generation < it->generation)
    {
        return PrepareResult::Stale;
    }
    if (it->browserWindowId != candidate.browserWindowId ||
        it->tabId != candidate.tabId ||
        it->channel.compare(candidate.channel, Qt::CaseInsensitive) != 0)
    {
        return PrepareResult::Conflict;
    }
    it->leaseExpiresAt = std::max(it->leaseExpiresAt, candidate.leaseExpiresAt);
    return PrepareResult::AlreadyCurrent;
}

bool AttachmentSessionRegistry::markReady(const QString &sessionId,
                                          qint64 generation)
{
    QMutexLocker lock(&this->mutex_);
    const auto it = this->sessions_.find(sessionId);
    if (it == this->sessions_.end() || it->generation != generation)
    {
        return false;
    }
    it->ready = true;
    return true;
}

bool AttachmentSessionRegistry::remove(const QString &sessionId,
                                       qint64 generation)
{
    QMutexLocker lock(&this->mutex_);
    const auto it = this->sessions_.find(sessionId);
    if (it == this->sessions_.end() || it->generation != generation)
    {
        return false;
    }
    this->sessions_.erase(it);
    return true;
}

std::vector<AttachmentSession> AttachmentSessionRegistry::expire(qint64 now)
{
    std::vector<AttachmentSession> expired;
    QMutexLocker lock(&this->mutex_);
    for (auto it = this->sessions_.begin(); it != this->sessions_.end();)
    {
        if (it->leaseExpiresAt > 0 && it->leaseExpiresAt <= now)
        {
            expired.emplace_back(*it);
            it = this->sessions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return expired;
}

std::optional<AttachmentSession>
    AttachmentSessionRegistry::uniqueReadyForChannel(
        const QString &channel) const
{
    QMutexLocker lock(&this->mutex_);
    std::optional<AttachmentSession> result;
    for (const auto &session : this->sessions_)
    {
        if (!session.ready ||
            (session.leaseExpiresAt > 0 &&
             session.leaseExpiresAt <= currentUtcMs()) ||
            session.channel.compare(channel, Qt::CaseInsensitive) != 0)
        {
            continue;
        }
        if (result)
        {
            return std::nullopt;
        }
        result = session;
    }
    return result;
}

bool AttachmentSessionRegistry::containsReady(const QString &sessionId,
                                              qint64 generation) const
{
    QMutexLocker lock(&this->mutex_);
    const auto it = this->sessions_.find(sessionId);
    return it != this->sessions_.end() && it->generation == generation &&
           it->ready;
}

bool AttachmentSessionRegistry::matchesReadyIdentity(
    const AttachmentSession &candidate) const
{
    QMutexLocker lock(&this->mutex_);
    const auto it = this->sessions_.find(candidate.sessionId);
    return it != this->sessions_.end() && it->ready &&
           (it->leaseExpiresAt <= 0 || it->leaseExpiresAt > currentUtcMs()) &&
           it->generation == candidate.generation &&
           it->browserWindowId == candidate.browserWindowId &&
           it->tabId == candidate.tabId &&
           it->channel.compare(candidate.channel, Qt::CaseInsensitive) == 0;
}

bool AttachmentSessionRegistry::empty() const
{
    QMutexLocker lock(&this->mutex_);
    return this->sessions_.isEmpty();
}

// CLIENT

namespace nm::client {

void sendMessage(const QByteArray &array)
{
    const auto status = ipc::sendMessage("chatterino_gui", array);
    if (status != ipc::DeliveryStatus::Delivered)
    {
        std::osyncstream(std::cerr) << "[BrowserExtension] failed to deliver message to "
                     "chatterino_gui; status="
                  << static_cast<int>(status) << std::endl;
    }
}

void writeToCout(const QByteArray &array)
{
    const auto *data = array.data();
    auto size = uint32_t(array.size());

    // We're writing the raw bytes to cout.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::cout.write(reinterpret_cast<char *>(&size), 4);
    std::cout.write(data, size);
    std::cout.flush();
}

}  // namespace nm::client

void sendToBrowserExtension(const QJsonObject &obj)
{
    auto *app = tryGetApp();
    if (!app)
    {
        std::cerr << "[STDERR] sendToBrowserExtension: FAILED - app unavailable" << std::endl;
        qCWarning(chatterinoNativeMessage)
            << "sendToBrowserExtension: failed - app unavailable";
        return;
    }

    const auto &paths = app->getPaths();
    (void)paths;
    const auto message = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    std::cerr << "[STDERR] sendToBrowserExtension: attempting to send to IPC queue 'chatterino_browser' - message: "
              << message.toStdString() << std::endl;

    qCDebug(chatterinoNativeMessage)
        << "sendToBrowserExtension: sending to IPC queue 'chatterino_browser' - message:"
        << message;

    // Check delivery status and retry on failure
    constexpr int MAX_RETRIES = 3;
    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt)
    {
        std::cerr << "[STDERR] sendToBrowserExtension: attempt " << attempt << "/" << MAX_RETRIES << std::endl;

        auto status = ipc::sendMessage(BROWSER_IPC_QUEUE_NAME, message);

        if (status == ipc::DeliveryStatus::Delivered)
        {
            std::cerr << "[STDERR] sendToBrowserExtension: SUCCESS - delivered on attempt " << attempt << std::endl;
            if (attempt > 1)
            {
                qCDebug(chatterinoNativeMessage)
                    << "sendToBrowserExtension: delivered on retry" << attempt;
            }
            return;
        }

        // Log failure with specific reason
        const char *reason = "unknown";
        switch (status)
        {
            case ipc::DeliveryStatus::QueueUnavailable:
                reason = "queue unavailable (native host not running?)";
                break;
            case ipc::DeliveryStatus::QueueFull:
                reason = "queue full (100 messages backlog)";
                break;
            case ipc::DeliveryStatus::InvalidMessage:
                reason = "invalid message (empty or > 1024 bytes)";
                break;
            default:
                break;
        }

        if (attempt < MAX_RETRIES)
        {
            std::cerr << "[STDERR] sendToBrowserExtension: delivery FAILED - " << reason
                      << " - retrying (" << attempt << "/" << MAX_RETRIES << ")" << std::endl;
            qCWarning(chatterinoNativeMessage)
                << "sendToBrowserExtension: delivery failed -" << reason
                << "- retrying (" << attempt << "/" << MAX_RETRIES << ")";
            // Brief delay before retry
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        else
        {
            std::cerr << "[STDERR] sendToBrowserExtension: delivery FAILED after " << MAX_RETRIES
                      << " attempts - " << reason << " - message LOST" << std::endl;
            qCWarning(chatterinoNativeMessage)
                << "sendToBrowserExtension: delivery failed after" << MAX_RETRIES
                << "attempts -" << reason << "- message lost";
        }
    }
}

// SERVER
NativeMessagingServer::NativeMessagingServer()
    : thread(new ReceiverThread(*this))
{
    instance_ = this;
    this->thread->setObjectName("C2NMReceiver");
}

NativeMessagingServer *NativeMessagingServer::instance()
{
    return instance_;
}

bool NativeMessagingServer::isBrowserAttached()
{
    if (!instance_)
    {
        return false;
    }
    return instance_->legacyBrowserAttached_ || !instance_->sessions_.empty();
}

ipc::DeliveryStatus NativeMessagingServer::sendNativeChat(
    const QString &channel, const QString &message, const QString &requestId)
{
    if (!instance_ || message.trimmed().isEmpty() || requestId.isEmpty())
    {
        return ipc::DeliveryStatus::InvalidMessage;
    }

    const auto session = instance_->sessions_.uniqueReadyForChannel(channel);
    if (!session)
    {
        // Do not guess between two valid windows with the same channel.
        return ipc::DeliveryStatus::InvalidMessage;
    }

    QJsonObject payload{{u"action"_s, u"sendNativeChat"_s},
                        {u"protocolVersion"_s, 2},
                        {u"message"_s, message},
                        {u"channel"_s, channel},
                        {u"requestId"_s, requestId},
                        {u"sessionId"_s, session->sessionId},
                        {u"browserWindowId"_s, session->browserWindowId},
                        {u"tabId"_s, session->tabId},
                        {u"generation"_s, session->generation}};
    return ipc::sendMessage(
        BROWSER_IPC_QUEUE_NAME,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

NativeMessagingServer::~NativeMessagingServer()
{
    instance_ = nullptr;
    this->thread->requestInterruption();
    // Wake the upstream-style blocking receiver so it can observe the
    // interruption request and shut down without force-terminating the
    // QThread.
    (void)ipc::sendMessage(
        "chatterino_gui",
        QByteArrayLiteral(R"({"action":"sync","twitchChannels":[]})"));
    if (!this->thread->wait(1500))
    {
        qCWarning(chatterinoNativeMessage)
            << "Native messaging receiver did not stop cooperatively";
    }
    if (!ipc::IpcQueue::remove("chatterino_gui"))
    {
        qCWarning(chatterinoNativeMessage) << "Failed to remove message queue";
    }
}

void NativeMessagingServer::start()
{
    this->thread->start();
}

NativeMessagingServer::ReceiverThread::ReceiverThread(
    NativeMessagingServer &parent)
    : parent_(parent)
{
}

void NativeMessagingServer::ReceiverThread::run()
{
    // The GUI is the sole creator and drainer of this queue; hosts only write
    // to it. A previous GUI that was force-killed (or a host that died mid
    // send) can leave the file-backed queue with a permanently locked
    // emulated interprocess mutex, after which every send or receive on it
    // hangs. Replace the queue so this GUI always starts from a clean one.
    auto [messageQueue, error] =
        ipc::IpcQueue::tryReplaceOrCreate("chatterino_gui", 100, MESSAGE_SIZE);

    if (!error.isEmpty())
    {
        qCDebug(chatterinoNativeMessage)
            << "Failed to create message queue:" << error;

        nmIpcError().set(error);
        return;
    }

    // A native host may already have forwarded the initial selection before
    // this queue existed. Announce that the desktop receiver is ready so the
    // extension can request and replay fresh geometry.
    ipc::sendMessage(
        BROWSER_IPC_QUEUE_NAME,
        QByteArrayLiteral(
            R"({"type":"status","status":"desktop-ready","protocolVersion":2,"capabilities":["sessions"]})"));

    while (!this->isInterruptionRequested())
    {
        auto buf = messageQueue->receive();
        if (buf.isEmpty())
        {
            continue;
        }
        auto document = QJsonDocument::fromJson(buf);

        this->handleMessage(document.object());
        this->expireSessions();
    }
}

void NativeMessagingServer::ReceiverThread::handleMessage(
    const QJsonObject &root)
{
    const auto message = nm::parseNativeMessage(root);
    if (!message)
    {
        qCDebug(chatterinoNativeMessage)
            << "NM rejected message:" << message.error;

        // A parse failure used to be dropped silently, which left the browser
        // waiting forever on an acknowledgement it would never receive: the
        // extension reported a healthy connection while chat never attached.
        // Report it so the failure is observable and the overlay can fail open.
        if (root["protocolVersion"_L1].toVariant().toLongLong() >= 2)
        {
            const auto identity = nm::parseAttachmentIdentity(root);
            if (!identity.sessionId.isEmpty())
            {
                AttachmentSession session;
                session.sessionId = identity.sessionId;
                session.browserWindowId = identity.browserWindowId;
                session.tabId = identity.tabId;
                session.generation = identity.generation;
                session.channel = identity.channel;
                session.winId =
                    root["winId"_L1].toString(identity.browserWindowId);
                this->parent_.reportSession(
                    session, u"attachment-rejected"_s, u"malformed-message"_s,
                    root["requestId"_L1].toString(
                        root["attachRequestId"_L1].toString()));
            }
        }
        return;
    }

    switch (message.action)
    {
        case nm::NativeAction::Select:
            this->handleSelect(root);
            return;
        case nm::NativeAction::Detach:
            this->handleDetach(root);
            return;
        case nm::NativeAction::Sync:
            this->handleSync(root);
            return;
        case nm::NativeAction::Engagement:
        case nm::NativeAction::PredictionLegacy:
            this->handleEngagement(root);
            return;
        case nm::NativeAction::Pin:
            this->handlePinnedMessage(root);
            return;
        case nm::NativeAction::RewardPending:
            this->handleRewardPending(root);
            return;
        case nm::NativeAction::RewardClear:
            this->handleRewardClear(root);
            return;
        case nm::NativeAction::LeaseRenew:
            this->handleLeaseRenew(root);
            return;
        case nm::NativeAction::Reconcile:
            this->handleReconcile(root);
            return;
        case nm::NativeAction::NativeChatResult:
            this->handleNativeChatResult(root);
            return;
        case nm::NativeAction::Unknown:
            break;
    }
}

// NOLINTBEGIN(readability-convert-member-functions-to-static)
void NativeMessagingServer::ReceiverThread::handleSelect(
    const QJsonObject &root)
{
    const auto parsed = nm::parseNativeMessage(root);
    const bool v2 = parsed.protocolVersion >= 2;
    QString type = root["type"_L1].toString();
    bool attach = root["attach"_L1].toBool();
    bool attachFullscreen = root["attach_fullscreen"_L1].toBool();
    QString name = root["name"_L1].toString(root["channel"_L1].toString());
    QString attachRequestId = root["attachRequestId"_L1].toString();
    const QString requestId = root["requestId"_L1].toString(attachRequestId);
    AttachmentSession session;
    if (v2)
    {
        const auto identity = nm::parseAttachmentIdentity(root);
        session.sessionId = identity.sessionId;
        session.browserWindowId = identity.browserWindowId;
        session.tabId = identity.tabId;
        session.generation = identity.generation;
        session.channel = identity.channel;
        session.leaseExpiresAt = leaseExpiry(root);
        session.winId = root["winId"_L1].toString(session.browserWindowId);

        std::cerr << "[STDERR] handleSelect: received select message - "
                  << "winId: " << session.winId.toStdString()
                  << ", sessionId: " << session.sessionId.toStdString()
                  << ", generation: " << session.generation
                  << ", browserWindowId: " << session.browserWindowId.toStdString()
                  << ", tabId: " << session.tabId
                  << ", channel: " << session.channel.toStdString()
                  << ", requestId: " << requestId.toStdString()
                  << ", v2Protocol: " << v2 << std::endl;

        qCDebug(chatterinoNativeMessage)
            << "handleSelect: received select message - winId:" << session.winId
            << "sessionId:" << session.sessionId
            << "generation:" << session.generation
            << "browserWindowId:" << session.browserWindowId
            << "tabId:" << session.tabId
            << "channel:" << session.channel
            << "requestId:" << requestId
            << "v2Protocol:" << v2;
    }
    else
    {
        std::cerr << "[STDERR] handleSelect: received select message (legacy) - "
                  << "winId: " << root["winId"_L1].toString().toStdString()
                  << ", channel: " << name.toStdString()
                  << ", requestId: " << requestId.toStdString() << std::endl;

        qCDebug(chatterinoNativeMessage)
            << "handleSelect: received select message (legacy) - winId:" << root["winId"_L1].toString()
            << "channel:" << name
            << "requestId:" << requestId;
    }

#ifdef USEWINSDK
    const auto sizeObject = root["size"_L1].toObject();
    AttachedWindow::GetArgs args = {
        .winId =
            root["winId"_L1].toString(v2 ? session.browserWindowId : QString()),
        .yOffset = root["yOffset"_L1].toInt(-1),
        .x = sizeObject["x"_L1].toDouble(-1.0),
        .pixelRatio = sizeObject["pixelRatio"_L1].toDouble(-1.0),
        .width = sizeObject["width"_L1].toInt(-1),
        .height = sizeObject["height"_L1].toInt(-1),
        .fullscreen = attachFullscreen,
    };
    if (v2)
    {
        args.sessionId = session.sessionId;
        args.generation = session.generation;
        args.onLoss = [](QString sessionId, qint64 generation, QString reason) {
            if (auto *server = NativeMessagingServer::instance())
            {
                server->loseSession(sessionId, generation, reason);
            }
        };
    }

    qCDebug(chatterinoNativeMessage)
        << args.x << args.pixelRatio << args.width << args.height << args.winId;

    if (args.winId.isNull())
    {
        std::cerr << "[STDERR] handleSelect: REJECTED - winId is missing" << std::endl;
        qCDebug(chatterinoNativeMessage) << "winId in select is missing";
        if (v2)
        {
            this->parent_.reportSession(session, u"attachment-rejected"_s,
                                        u"missing-winid"_s, requestId);
        }
        return;
    }

    void *browserTarget = nullptr;
    bool browserHwndOk = false;
    const auto browserHwnd =
        root["browserHwnd"_L1].toString().toULongLong(&browserHwndOk);
    if (browserHwndOk && browserHwnd != 0)
    {
        auto target =
            reinterpret_cast<HWND>(static_cast<quintptr>(browserHwnd));
        if (::IsWindow(target))
        {
            browserTarget = target;
            session.browserHwnd = browserHwnd;
            std::cerr << "[STDERR] handleSelect: browserHwnd validation SUCCESS - hwnd: "
                      << browserHwnd << std::endl;
            qCDebug(chatterinoNativeMessage)
                << "handleSelect: browserHwnd validation SUCCESS - hwnd:" << browserHwnd;
        }
        else
        {
            std::cerr << "[STDERR] handleSelect: browserHwnd validation FAILED - IsWindow returned false for hwnd: "
                      << browserHwnd << std::endl;
            qCDebug(chatterinoNativeMessage)
                << "handleSelect: browserHwnd validation FAILED - IsWindow returned false for hwnd:" << browserHwnd;
        }
    }
    else
    {
        std::cerr << "[STDERR] handleSelect: browserHwnd validation FAILED - parsing failed or zero value, browserHwndOk: "
                  << browserHwndOk << ", browserHwnd: " << browserHwnd << std::endl;
        qCDebug(chatterinoNativeMessage)
            << "handleSelect: browserHwnd validation FAILED - parsing failed or zero value, browserHwndOk:" << browserHwndOk
            << "browserHwnd:" << browserHwnd;
    }

    // Chromium can launch the native host with --parent-window=0. Preserve
    // Chatterino's original foreground-window attach behavior, but capture
    // and validate the HWND here before the GUI-thread handoff. Looking up the
    // foreground window later can bind the overlay to whichever application
    // the user focused in the meantime, which also breaks move/resize tracking.
    if ((attach || attachFullscreen) && browserTarget == nullptr)
    {
        auto foreground = ::GetForegroundWindow();
        const bool foregroundWasReportedByBrowser =
            !v2 || root["browserWindowFocused"_L1].toBool();
        if (foregroundWasReportedByBrowser &&
            isSupportedOverlayTarget(foreground))
        {
            browserTarget = foreground;
            session.browserHwnd = reinterpret_cast<quintptr>(foreground);
        }
    }

#endif

    if (type != u"twitch"_s)
    {
        std::cerr << "[STDERR] handleSelect: REJECTED - unknown channel type: "
                  << type.toStdString() << std::endl;
        qCDebug(chatterinoNativeMessage) << "NM unknown channel type";
        if (v2)
        {
            this->parent_.reportSession(session, u"attachment-rejected"_s,
                                        u"unknown-channel-type"_s, requestId);
        }
        return;
    }

#ifdef USEWINSDK
    if ((attach || attachFullscreen) &&
        (browserTarget == nullptr ||
         !isSupportedOverlayTarget(HWND(browserTarget))))
    {
        std::cerr << "[STDERR] handleSelect: REJECTED - no validated browser target" << std::endl;
        qCDebug(chatterinoNativeMessage)
            << "handleSelect: rejecting attachment - no validated browser target";
        if (v2)
        {
            this->parent_.reportSession(session, u"attachment-rejected"_s,
                                        u"invalid-browser-hwnd"_s, requestId);
        }
        return;
    }
#elif !defined(USEWINSDK)
    if (v2 && (attach || attachFullscreen))
    {
        std::cerr << "[STDERR] handleSelect: REJECTED - unsupported platform (non-Windows)" << std::endl;
        qCDebug(chatterinoNativeMessage)
            << "handleSelect: rejecting attachment - unsupported platform (non-Windows)";
        this->parent_.reportSession(session, u"attachment-rejected"_s,
                                    u"unsupported-platform"_s, requestId);
        return;
    }
#endif

    AttachmentSessionRegistry::PrepareResult prepared =
        AttachmentSessionRegistry::PrepareResult::Prepared;
    std::optional<AttachmentSession> replaced;
    if (v2)
    {
        prepared = this->parent_.sessions_.prepare(session, &replaced);
        if (prepared == AttachmentSessionRegistry::PrepareResult::Stale ||
            prepared == AttachmentSessionRegistry::PrepareResult::Conflict)
        {
            this->parent_.reportSession(
                session, u"attachment-rejected"_s,
                prepared == AttachmentSessionRegistry::PrepareResult::Stale
                    ? u"stale-generation"_s
                    : u"session-identity-conflict"_s,
                requestId);
            return;
        }
        // The session identity stays the same when the browser resizes or
        // changes zoom/fullscreen state. Even an already-ready session must
        // apply the new geometry on the GUI thread before acknowledging this
        // select; acknowledging here leaves the overlay at its old position.
    }

    postToThread([=, &parent = this->parent_] {
        // Legacy peers retained a single watching-channel model. v2 overlays
        // never update it: the session owns channel routing instead.
        if (!v2 && !name.isEmpty())
        {
            auto channel = getApp()->getTwitch()->getOrAddChannel(name);
            if (getApp()->getTwitch()->getWatchingChannel().get() != channel)
            {
                getApp()->getTwitch()->setWatchingChannel(channel);
            }
        }

        if (attach || attachFullscreen)
        {
            parent.legacyBrowserAttached_ = !v2;
#ifdef USEWINSDK
            if (replaced && replaced->browserHwnd != session.browserHwnd)
            {
                AttachedWindow::detach(replaced->winId, replaced->sessionId);
            }
            auto *window = AttachedWindow::get(browserTarget, args);
            if (!name.isEmpty())
            {
                window->setChannel(
                    getApp()->getTwitch()->getOrAddChannel(name));
            }

            if (v2)
            {
                if (!parent.sessions_.markReady(session.sessionId,
                                                session.generation))
                {
                    // A newer select won while the GUI job was queued.
                    AttachedWindow::detach(args.winId, session.sessionId);
                    return;
                }
                std::cerr << "[STDERR] handleSelect: marked session ready, calling reportSession with 'chat-attached' - "
                          << "sessionId: " << session.sessionId.toStdString()
                          << ", generation: " << session.generation << std::endl;
                qCDebug(chatterinoNativeMessage)
                    << "handleSelect: marked session ready, calling reportSession with 'chat-attached' - sessionId:"
                    << session.sessionId << "generation:" << session.generation;
                parent.reportSession(session, u"chat-attached"_s, {},
                                     requestId);
            }
            else if (!attachRequestId.isEmpty())
            {
                sendToBrowserExtension(QJsonObject{
                    {u"type"_s, "status"_L1},
                    {u"status"_s, "chat-attached"_L1},
                    {u"winId"_s, args.winId},
                    {u"attachRequestId"_s, attachRequestId},
                });
            }
#endif
        }
    });
}

void NativeMessagingServer::ReceiverThread::handleDetach(
    const QJsonObject &root)
{
    const auto parsed = nm::parseNativeMessage(root);
    if (parsed.protocolVersion >= 2)
    {
        const auto identity = nm::parseAttachmentIdentity(root);
        AttachmentSession session;
        session.sessionId = identity.sessionId;
        session.browserWindowId = identity.browserWindowId;
        session.tabId = identity.tabId;
        session.generation = identity.generation;
        session.channel = identity.channel;
        session.winId = root["winId"_L1].toString(identity.browserWindowId);
        const auto requestId = root["requestId"_L1].toString(
            root["attachRequestId"_L1].toString());
        if (!this->parent_.sessions_.remove(session.sessionId,
                                            session.generation))
        {
            this->parent_.reportSession(session, u"attachment-rejected"_s,
                                        u"stale-generation"_s, requestId);
            return;
        }
#ifdef USEWINSDK
        postToThread([session, &parent = this->parent_] {
            AttachedWindow::detach(session.winId, session.sessionId);
            parent.reportSession(session, u"detached"_s);
        });
#else
        this->parent_.reportSession(session, u"detached"_s, {}, requestId);
#endif
        return;
    }

    QString winId = root["winId"_L1].toString();

    if (winId.isNull())
    {
        qCDebug(chatterinoNativeMessage) << "NM winId missing";
        return;
    }

#ifdef USEWINSDK
    postToThread([winId, &parent = this->parent_] {
        qCDebug(chatterinoNativeMessage) << "NW detach";
        parent.legacyBrowserAttached_ = false;
        AttachedWindow::detach(winId);
    });
#endif
}
// NOLINTEND(readability-convert-member-functions-to-static)

void NativeMessagingServer::ReceiverThread::handleLeaseRenew(
    const QJsonObject &root)
{
    const auto identity = nm::parseAttachmentIdentity(root);
    AttachmentSession session;
    session.sessionId = identity.sessionId;
    session.browserWindowId = identity.browserWindowId;
    session.tabId = identity.tabId;
    session.generation = identity.generation;
    session.channel = identity.channel;
    session.leaseExpiresAt = leaseExpiry(root);
    const auto result = this->parent_.sessions_.renewLease(session);
    this->parent_.reportSession(
        session,
        result == AttachmentSessionRegistry::PrepareResult::Stale ||
                result == AttachmentSessionRegistry::PrepareResult::Conflict
            ? u"attachment-rejected"_s
            : u"lease-renewed"_s,
        result == AttachmentSessionRegistry::PrepareResult::Stale
            ? u"stale-generation"_s
        : result == AttachmentSessionRegistry::PrepareResult::Conflict
            ? u"session-identity-conflict"_s
            : QStringView{},
        root["requestId"_L1].toString());
}

void NativeMessagingServer::ReceiverThread::handleReconcile(
    const QJsonObject &root)
{
    // This acknowledgement is deliberately observational. The extension owns
    // desired state and will resend a select for anything it still needs.
    sendToBrowserExtension(
        QJsonObject{{u"type"_s, u"status"_s},
                    {u"protocolVersion"_s, 2},
                    {u"status"_s, u"reconcile"_s},
                    {u"requestId"_s, root["requestId"_L1].toString()}});
}

void NativeMessagingServer::ReceiverThread::handleNativeChatResult(
    const QJsonObject &root)
{
    // Result messages are acknowledgements of browser dispatch, not evidence
    // that Twitch accepted a message. Never retry an uncertain acceptance.
    const auto identity = nm::parseAttachmentIdentity(root);
    AttachmentSession candidate{
        .sessionId = identity.sessionId,
        .browserWindowId = identity.browserWindowId,
        .tabId = identity.tabId,
        .generation = identity.generation,
        .channel = identity.channel,
    };
    if (!this->parent_.sessions_.matchesReadyIdentity(candidate))
    {
        return;
    }
    const auto status = root["status"_L1].toString();
    if (status != u"accepted"_s && status != u"rejected"_s &&
        status != u"uncertain"_s)
    {
        return;
    }
    qCDebug(chatterinoNativeMessage)
        << "Native chat result" << status << root["requestId"_L1].toString()
        << identity.sessionId;
    sendToBrowserExtension(QJsonObject{
        {u"action"_s, u"nativeChatResult"_s},
        {u"protocolVersion"_s, 2},
        {u"status"_s, status},
        {u"requestId"_s, root["requestId"_L1].toString()},
        {u"sessionId"_s, identity.sessionId},
        {u"browserWindowId"_s, identity.browserWindowId},
        {u"tabId"_s, identity.tabId},
        {u"generation"_s, identity.generation},
        {u"reason"_s, root["reason"_L1].toString()},
    });
}

void NativeMessagingServer::ReceiverThread::expireSessions()
{
    for (const auto &session : this->parent_.sessions_.expire(currentUtcMs()))
    {
#ifdef USEWINSDK
        postToThread([session, &parent = this->parent_] {
            AttachedWindow::detach(session.winId, session.sessionId);
            parent.reportSession(session, u"attachment-lost"_s,
                                 u"lease-expired"_s);
        });
#else
        this->parent_.reportSession(session, u"attachment-lost"_s,
                                    u"lease-expired"_s);
#endif
    }
}

void NativeMessagingServer::ReceiverThread::handleSync(const QJsonObject &root)
{
    // Structure:
    // { action: 'sync', twitchChannels?: string[] }
    postToThread([&parent = this->parent_,
                  twitch = root["twitchChannels"_L1].toArray()] {
        parent.syncChannels(twitch);
    });
}

void NativeMessagingServer::reportSession(const AttachmentSession &session,
                                          QStringView status,
                                          QStringView reason,
                                          QStringView requestId)
{
    std::cerr << "[STDERR] reportSession: called with status: " << status.toString().toStdString()
              << ", sessionId: " << session.sessionId.toStdString()
              << ", generation: " << session.generation
              << ", browserWindowId: " << session.browserWindowId.toStdString()
              << ", tabId: " << session.tabId
              << ", winId: " << session.winId.toStdString()
              << ", reason: " << reason.toString().toStdString()
              << ", requestId: " << requestId.toString().toStdString() << std::endl;

    qCDebug(chatterinoNativeMessage)
        << "reportSession: called with status:" << status.toString()
        << "sessionId:" << session.sessionId
        << "generation:" << session.generation
        << "browserWindowId:" << session.browserWindowId
        << "tabId:" << session.tabId
        << "winId:" << session.winId
        << "reason:" << reason.toString()
        << "requestId:" << requestId.toString();

    QJsonObject payload{{u"type"_s, u"status"_s},
                        {u"protocolVersion"_s, 2},
                        {u"status"_s, status.toString()},
                        {u"sessionId"_s, session.sessionId},
                        {u"browserWindowId"_s, session.browserWindowId},
                        {u"tabId"_s, session.tabId},
                        {u"generation"_s, session.generation},
                        {u"winId"_s, session.winId}};
    if (session.leaseExpiresAt > 0)
    {
        payload.insert(u"leaseExpiresAt"_s, session.leaseExpiresAt);
    }
    if (!reason.isEmpty())
    {
        payload.insert(u"reason"_s, reason.toString());
    }
    if (!requestId.isEmpty())
    {
        payload.insert(u"requestId"_s, requestId.toString());
        payload.insert(u"attachRequestId"_s, requestId.toString());
    }

    const auto jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    std::cerr << "[STDERR] reportSession: sending JSON to browser extension: "
              << jsonBytes.toStdString() << std::endl;

    qCDebug(chatterinoNativeMessage)
        << "reportSession: sending JSON to browser extension:"
        << jsonBytes;

    sendToBrowserExtension(payload);
#ifdef CHATTERINO_HAVE_PLUGINS
    dispatchBetterBrowserEvent(BetterBrowserEvent{
        .event = u"attachment"_s,
        .sessionId = session.sessionId,
        .generation = session.generation,
        .channel = session.channel,
        .source = u"native-messaging"_s,
        .status = status.toString(),
        .reason = reason.toString(),
    });
#endif
}

void NativeMessagingServer::loseSession(const QString &sessionId,
                                        qint64 generation, QStringView reason)
{
    // The registry removal is generation-exact, so a late overlay-death event
    // cannot tear down a newer replacement session.
    if (!this->sessions_.remove(sessionId, generation))
    {
        return;
    }
    AttachmentSession session;
    session.sessionId = sessionId;
    session.generation = generation;
    this->reportSession(session, u"attachment-lost"_s, reason);
}

void NativeMessagingServer::ReceiverThread::handleEngagement(
    const QJsonObject &root)
{
    postToThread([&parent = this->parent_, root] {
        parent.updateEngagement(root);
    });
}

void NativeMessagingServer::ReceiverThread::handlePinnedMessage(
    const QJsonObject &root)
{
    postToThread([&parent = this->parent_, root] {
        parent.updatePinnedMessage(root);
    });
}

void NativeMessagingServer::updatePinnedMessage(const QJsonObject &root)
{
    assertInGuiThread();

    QString text = root["message"_L1].toString();
    QString channelName = root["channel"_L1].toString();

    ChannelPtr channel;
    if (!channelName.isEmpty())
    {
        channel = getApp()->getTwitch()->getOrAddChannel(channelName);
    }
    else
    {
        channel = getApp()->getTwitch()->getWatchingChannel().get();
    }

    if (!channel || channel->isEmpty())
    {
        return;
    }

    // Pinned messages and predictions are independent now: the prediction has
    // its own banner above the chat input, so it never hijacks the pin.
    channel->setPinnedMessageText(text);
}

void NativeMessagingServer::updateEngagement(const QJsonObject &root)
{
    assertInGuiThread();

    const auto channelName = root["channel"_L1].toString();
    ChannelPtr channel =
        channelName.isEmpty()
            ? getApp()->getTwitch()->getWatchingChannel().get()
            : getApp()->getTwitch()->getOrAddChannel(channelName);
    if (!channel || channel->isEmpty())
    {
        return;
    }

    const auto kind = root["kind"_L1].toString().toLower() == u"poll"
                          ? EngagementKind::Poll
                          : EngagementKind::Prediction;
    const auto lifecycle =
        root["lifecycle"_L1].toString(QStringLiteral("upsert"));
    if (lifecycle == QStringLiteral("remove"))
    {
        channel->clearEngagement(kind);
#ifdef CHATTERINO_HAVE_PLUGINS
        dispatchBetterBrowserEvent(BetterBrowserEvent{
            .event = u"activity"_s,
            .sessionId = root["sessionId"_L1].toString(),
            .generation = root["generation"_L1].toInteger(),
            .channel = channelName,
            .source = root["source"_L1].toString(u"browser"_s),
            .status = u"removed"_s,
            .activityKind = root["kind"_L1].toString(),
            .activityStatus = u"removed"_s,
        });
#endif
        return;
    }

    EngagementState state;
    state.title = root["title"_L1].toString().trimmed();
    state.status = root["status"_L1].toString(QStringLiteral("started"));
    state.winner = root["winner"_L1].toString();
    for (const auto &option : root["options"_L1].toArray())
    {
        const auto text = option.toString().trimmed();
        if (!text.isEmpty())
        {
            state.options.append(text);
        }
    }
    const auto &previous = channel->getEngagement(kind);
    const auto duration = root["duration"_L1].toInt(0);
    if (state.status == QStringLiteral("started"))
    {
        const auto closesAtValue = root["closesAt"_L1];
        qint64 closesAtMs = 0;
        if (closesAtValue.isDouble())
        {
            closesAtMs = static_cast<qint64>(closesAtValue.toDouble());
        }
        else if (closesAtValue.isString())
        {
            closesAtMs = closesAtValue.toString().toLongLong();
        }

        if (closesAtMs > 0)
        {
            state.closesAt =
                QDateTime::fromMSecsSinceEpoch(closesAtMs, Qt::UTC);
        }
        else if (duration > 0)
        {
            state.closesAt = QDateTime::currentDateTimeUtc().addSecs(duration);
        }
        else if (previous && previous->title == state.title &&
                 previous->status == state.status)
        {
            state.closesAt = previous->closesAt;
        }
    }

    const bool transition = !previous || previous->title != state.title ||
                            previous->status != state.status ||
                            previous->winner != state.winner;
    channel->setEngagement(kind, state);

#ifdef CHATTERINO_HAVE_PLUGINS
    if (transition)
    {
        dispatchBetterBrowserEvent(BetterBrowserEvent{
            .event = u"activity"_s,
            .sessionId = root["sessionId"_L1].toString(),
            .generation = root["generation"_L1].toInteger(),
            .channel = channelName,
            .source = root["source"_L1].toString(u"browser"_s),
            .status = u"updated"_s,
            .activityKind = root["kind"_L1].toString(),
            .activityTitle = state.title,
            .activityStatus = state.status,
        });
    }
#endif

    if (transition)
    {
        const auto text = formatEngagement(kind, state);
        if (!text.isEmpty())
        {
            QColor highlight = kind == EngagementKind::Prediction
                                   ? QColor(0, 135, 90, 70)
                                   : QColor(26, 105, 255, 70);
            if (kind == EngagementKind::Poll &&
                state.status == QStringLiteral("locked"))
            {
                highlight = QColor(193, 125, 17, 70);
            }
            else if (state.status == QStringLiteral("ended"))
            {
                highlight = QColor(0, 158, 96, 70);
            }

            auto builder = MessageBuilder(systemMessage, text);
            builder->highlightColor = std::make_shared<QColor>(highlight);
            builder->flags.set(MessageFlag::Highlighted);
            channel->addMessage(builder.release(), MessageContext::Original);
        }
    }

    if (state.status == QStringLiteral("ended"))
    {
        const std::weak_ptr<Channel> weakChannel = channel;
        const auto title = state.title;
        QTimer::singleShot(30000, [weakChannel, kind, title] {
            const auto channel = weakChannel.lock();
            if (!channel)
            {
                return;
            }
            const auto &current = channel->getEngagement(kind);
            if (current && current->status == QStringLiteral("ended") &&
                current->title == title)
            {
                channel->clearEngagement(kind);
            }
        });
    }
}

void NativeMessagingServer::ReceiverThread::handleRewardPending(
    const QJsonObject &root)
{
    postToThread([&parent = this->parent_, root] {
        parent.updateRewardPending(root);
    });
}

void NativeMessagingServer::ReceiverThread::handleRewardClear(
    const QJsonObject &root)
{
    postToThread([&parent = this->parent_, root] {
        parent.clearRewardPending(root);
    });
}

void NativeMessagingServer::updateRewardPending(const QJsonObject &root)
{
    assertInGuiThread();

    QString channelName = root["channel"_L1].toString();
    QString rewardId = root["rewardId"_L1].toString();
    QString title = root["title"_L1].toString();
    QString prompt = root["prompt"_L1].toString();

    ChannelPtr channel;
    if (!channelName.isEmpty())
    {
        channel = getApp()->getTwitch()->getOrAddChannel(channelName);
    }
    else
    {
        channel = getApp()->getTwitch()->getWatchingChannel().get();
    }

    if (!channel)
    {
        return;
    }

    auto *twitchChannel = dynamic_cast<TwitchChannel *>(channel.get());
    if (!twitchChannel)
    {
        return;
    }

    twitchChannel->setPendingRewardRedemption(rewardId, title, prompt, 90);

    QString hint = title;
    if (hint.isEmpty())
    {
        hint = u"channel point reward"_s;
    }
    twitchChannel->addSystemMessage(
        QString("Send a message in chat to complete your redemption: %1")
            .arg(hint));
}

void NativeMessagingServer::clearRewardPending(const QJsonObject &root)
{
    assertInGuiThread();

    QString channelName = root["channel"_L1].toString();

    ChannelPtr channel;
    if (!channelName.isEmpty())
    {
        channel = getApp()->getTwitch()->getOrAddChannel(channelName);
    }
    else
    {
        channel = getApp()->getTwitch()->getWatchingChannel().get();
    }

    if (!channel)
    {
        return;
    }

    if (auto *twitchChannel = dynamic_cast<TwitchChannel *>(channel.get()))
    {
        twitchChannel->clearPendingRewardRedemption();
    }
}

void NativeMessagingServer::syncChannels(const QJsonArray &twitchChannels)
{
    assertInGuiThread();

    std::vector<ChannelPtr> updated;
    updated.reserve(twitchChannels.size());
    for (const auto value : twitchChannels)
    {
        auto name = value.toString();
        if (name.isEmpty())
        {
            continue;
        }
        // the deduping is done on the extension side
        updated.emplace_back(getApp()->getTwitch()->getOrAddChannel(name));
    }

    // This will destroy channels that aren't used anymore.
    this->channelWarmer_ = std::move(updated);
}

Atomic<std::optional<QString>> &nmIpcError()
{
    static Atomic<std::optional<QString>> x;
    return x;
}

}  // namespace chatterino
