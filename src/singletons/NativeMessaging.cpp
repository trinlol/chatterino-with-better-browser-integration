// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/NativeMessaging.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "common/Modes.hpp"
#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/IpcQueue.hpp"
#include "util/PostToThread.hpp"
#include "util/XDGDirectory.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/Message.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QStringBuilder>
#include <QTimer>

#include <iostream>

#ifdef Q_OS_WIN
#    include "widgets/AttachedWindow.hpp"
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

void registerNmManifest([[maybe_unused]] const Paths &paths,
                        const Config &config, const QJsonDocument &document)
{
#ifdef Q_OS_WIN
    std::ignore =
        writeManifestTo(paths.miscDirectory, u"."_s, config.fileName, document);

    QSettings registry(config.registryKey, QSettings::NativeFormat);
    registry.setValue("Default",
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
        u"chrome-extension://bogfpdfoagkaebimmlcbgmfmanhbhhlm/"_s};

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

void registerNmHost(const Paths &paths)
{
    if (Modes::instance().isPortable)
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

// CLIENT

namespace nm::client {

void sendMessage(const QByteArray &array)
{
    ipc::sendMessage("chatterino_gui", array);
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

// SERVER
NativeMessagingServer::NativeMessagingServer()
    : thread(new ReceiverThread(*this))
{
    this->thread->setObjectName("C2NMReceiver");
}

NativeMessagingServer::~NativeMessagingServer()
{
    if (this->predictionTimer_)
    {
        this->predictionTimer_->stop();
        delete this->predictionTimer_;
        this->predictionTimer_ = nullptr;
    }
    if (!ipc::IpcQueue::remove("chatterino_gui"))
    {
        qCWarning(chatterinoNativeMessage) << "Failed to remove message queue";
    }
    this->thread->requestInterruption();
    this->thread->quit();
    // Most likely, the receiver thread will still wait for a message
    if (!this->thread->wait(100))
    {
        this->thread->terminate();

        if (!this->thread->wait(100))
        {
            qCWarning(chatterinoNativeMessage)
                << "Failed to terminate thread cleanly";
        }
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
    auto [messageQueue, error] =
        ipc::IpcQueue::tryReplaceOrCreate("chatterino_gui", 100, MESSAGE_SIZE);

    if (!error.isEmpty())
    {
        qCDebug(chatterinoNativeMessage)
            << "Failed to create message queue:" << error;

        nmIpcError().set(error);
        return;
    }

    while (!this->isInterruptionRequested())
    {
        auto buf = messageQueue->receive();
        if (buf.isEmpty())
        {
            continue;
        }
        auto document = QJsonDocument::fromJson(buf);

        this->handleMessage(document.object());
    }
}

void NativeMessagingServer::ReceiverThread::handleMessage(
    const QJsonObject &root)
{
    QString action = root["action"_L1].toString();

    if (action == "select")
    {
        this->handleSelect(root);
        return;
    }
    if (action == "detach")
    {
        this->handleDetach(root);
        return;
    }
    if (action == "sync")
    {
        this->handleSync(root);
        return;
    }
    if (action == "prediction")
    {
        this->handlePrediction(root);
        return;
    }
    if (action == "pin")
    {
        this->handlePinnedMessage(root);
        return;
    }

    qCDebug(chatterinoNativeMessage) << "NM unknown action" << action;
}

// NOLINTBEGIN(readability-convert-member-functions-to-static)
void NativeMessagingServer::ReceiverThread::handleSelect(
    const QJsonObject &root)
{
    QString type = root["type"_L1].toString();
    bool attach = root["attach"_L1].toBool();
    bool attachFullscreen = root["attach_fullscreen"_L1].toBool();
    QString name = root["name"_L1].toString();

#ifdef USEWINSDK
    const auto sizeObject = root["size"_L1].toObject();
    AttachedWindow::GetArgs args = {
        .winId = root["winId"_L1].toString(),
        .yOffset = root["yOffset"_L1].toInt(-1),
        .x = sizeObject["x"_L1].toDouble(-1.0),
        .pixelRatio = sizeObject["pixelRatio"_L1].toDouble(-1.0),
        .width = sizeObject["width"_L1].toInt(-1),
        .height = sizeObject["height"_L1].toInt(-1),
        .fullscreen = attachFullscreen,
    };

    qCDebug(chatterinoNativeMessage)
        << args.x << args.pixelRatio << args.width << args.height << args.winId;

    if (args.winId.isNull())
    {
        qCDebug(chatterinoNativeMessage) << "winId in select is missing";
        return;
    }
#endif

    if (type != u"twitch"_s)
    {
        qCDebug(chatterinoNativeMessage) << "NM unknown channel type";
        return;
    }

    postToThread([=] {
        if (!name.isEmpty())
        {
            auto channel = getApp()->getTwitch()->getOrAddChannel(name);
            if (getApp()->getTwitch()->getWatchingChannel().get() != channel)
            {
                getApp()->getTwitch()->setWatchingChannel(channel);
            }
        }

        if (attach || attachFullscreen)
        {
#ifdef USEWINSDK
            auto *window = AttachedWindow::getForeground(args);
            if (!name.isEmpty())
            {
                window->setChannel(
                    getApp()->getTwitch()->getOrAddChannel(name));
            }
#endif
        }
    });
}

void NativeMessagingServer::ReceiverThread::handleDetach(
    const QJsonObject &root)
{
    QString winId = root["winId"_L1].toString();

    if (winId.isNull())
    {
        qCDebug(chatterinoNativeMessage) << "NM winId missing";
        return;
    }

#ifdef USEWINSDK
    postToThread([winId] {
        qCDebug(chatterinoNativeMessage) << "NW detach";
        AttachedWindow::detach(winId);
    });
#endif
}
// NOLINTEND(readability-convert-member-functions-to-static)

void NativeMessagingServer::ReceiverThread::handleSync(const QJsonObject &root)
{
    // Structure:
    // { action: 'sync', twitchChannels?: string[] }
    postToThread([&parent = this->parent_,
                  twitch = root["twitchChannels"_L1].toArray()] {
        parent.syncChannels(twitch);
    });
}

void NativeMessagingServer::ReceiverThread::handlePrediction(const QJsonObject &root)
{
    postToThread([&parent = this->parent_, root] {
        parent.updatePredictionSticky(root);
    });
}

void NativeMessagingServer::ReceiverThread::handlePinnedMessage(const QJsonObject &root)
{
    postToThread([&parent = this->parent_, root] {
        parent.updatePinnedMessage(root);
    });
}

void NativeMessagingServer::updatePinnedMessage(const QJsonObject &root)
{
    assertInGuiThread();

    QString text = root["message"_L1].toString();

    auto channel = getApp()->getTwitch()->getWatchingChannel().get();
    if (!channel || channel->isEmpty())
    {
        return;
    }

    channel->setPinnedMessageText(text);
}

void NativeMessagingServer::updatePredictionSticky(const QJsonObject &root)
{
    assertInGuiThread();

    QString status = root["status"_L1].toString();
    QString title = root["title"_L1].toString();
    QJsonArray options = root["options"_L1].toArray();
    int duration = root["duration"_L1].toInt(0);
    QString winner = root["winner"_L1].toString();

    auto channel = getApp()->getTwitch()->getWatchingChannel().get();
    if (!channel || channel->isEmpty())
    {
        return;
    }

    if (this->activeChannel_ && this->activeChannel_ != channel)
    {
        this->clearStickyMessage();
    }

    this->predictionTitle_ = title;
    this->predictionOptions_ = options;
    this->predictionStatus_ = status;
    this->predictionWinner_ = winner;
    this->activeChannel_ = channel;

    if (status == "started")
    {
        this->remainingSeconds_ = duration > 0 ? duration : 120;

        if (!this->predictionTimer_)
        {
            this->predictionTimer_ = new QTimer();
            QObject::connect(this->predictionTimer_, &QTimer::timeout, [this] {
                this->onPredictionTimerTick();
            });
        }
        this->predictionTimer_->start(1000);

        this->recreateStickyMessage();

        this->messageAppendedConnection_ = channel->messageAppended.connect([this](MessagePtr &msg, auto) {
            if (msg != this->activePredictionMessage_)
            {
                if (!this->recreateScheduled_)
                {
                    this->recreateScheduled_ = true;
                    QTimer::singleShot(0, [this] {
                        this->recreateScheduled_ = false;
                        this->recreateStickyMessage();
                    });
                }
            }
        });
    }
    else if (status == "locked")
    {
        this->remainingSeconds_ = 0;
        if (this->predictionTimer_)
        {
            this->predictionTimer_->stop();
        }
        this->recreateStickyMessage();
    }
    else if (status == "ended")
    {
        this->remainingSeconds_ = 0;
        if (this->predictionTimer_)
        {
            this->predictionTimer_->stop();
        }
        this->recreateStickyMessage();

        QTimer::singleShot(30000, [this] {
            this->clearStickyMessage();
        });
    }
}

void NativeMessagingServer::onPredictionTimerTick()
{
    assertInGuiThread();
    if (this->remainingSeconds_ > 0)
    {
        this->remainingSeconds_--;
        this->recreateStickyMessage();
    }
    else
    {
        if (this->predictionTimer_)
        {
            this->predictionTimer_->stop();
        }
    }
}

void NativeMessagingServer::recreateStickyMessage()
{
    assertInGuiThread();
    if (!this->activeChannel_)
    {
        return;
    }

    if (this->activePredictionMessage_)
    {
        auto emptyMsg = std::make_shared<Message>();
        this->activeChannel_->replaceMessage(this->activePredictionMessage_, emptyMsg);
        this->activePredictionMessage_.reset();
    }

    if (this->predictionStatus_ == "expired")
    {
        return;
    }

    QStringList optionTexts;
    for (const auto &val : this->predictionOptions_)
    {
        optionTexts.append(val.toString());
    }
    QString optionsStr = optionTexts.join(", ");

    QString text;
    if (this->predictionStatus_ == "started")
    {
        if (this->remainingSeconds_ > 0)
        {
            int mins = this->remainingSeconds_ / 60;
            int secs = this->remainingSeconds_ % 60;
            text = QString("Prediction: %1 | Options: %2 (%3:%4 remaining)")
                       .arg(this->predictionTitle_, optionsStr)
                       .arg(mins)
                       .arg(secs, 2, 10, QChar('0'));
        }
        else
        {
            text = QString("Prediction: %1 | Options: %2 (Locking...)").arg(this->predictionTitle_, optionsStr);
        }
    }
    else if (this->predictionStatus_ == "locked")
    {
        text = QString("Prediction Locked: %1 | Options: %2").arg(this->predictionTitle_, optionsStr);
    }
    else if (this->predictionStatus_ == "ended")
    {
        if (!this->predictionWinner_.isEmpty())
        {
            text = QString("Prediction Ended: %1 | Outcome: %2").arg(this->predictionTitle_, this->predictionWinner_);
        }
        else
        {
            text = QString("Prediction Ended: %1").arg(this->predictionTitle_);
        }
    }

    auto builder = MessageBuilder(systemMessage, text);
    builder->highlightColor = std::make_shared<QColor>(0, 150, 0, 80);
    builder->flags.set(MessageFlag::Highlighted);

    this->activePredictionMessage_ = builder.release();
    this->activeChannel_->addMessage(this->activePredictionMessage_, MessageContext::Original);
}

void NativeMessagingServer::clearStickyMessage()
{
    assertInGuiThread();
    this->predictionStatus_ = "expired";
    if (this->activeChannel_ && this->activePredictionMessage_)
    {
        auto emptyMsg = std::make_shared<Message>();
        this->activeChannel_->replaceMessage(this->activePredictionMessage_, emptyMsg);
        this->activePredictionMessage_.reset();
    }
    this->messageAppendedConnection_ = pajlada::Signals::ScopedConnection();
    this->activeChannel_.reset();
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
