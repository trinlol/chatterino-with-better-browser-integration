// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/streamelements/StreamElementsManager.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace chatterino {

namespace {

class StreamElementsListener final : public WebSocketListener
{
public:
    StreamElementsListener(StreamElementsManager *manager)
        : manager_(manager)
    {
    }

    void onOpen() override
    {
        qCDebug(chatterinoWebsocket) << "[StreamElements] Connected to Astro Gateway";
        QMetaObject::invokeMethod(this->manager_, [this] {
            this->manager_->handleConnected();
        });
    }

    void onTextMessage(QByteArray data) override
    {
        auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
        {
            return;
        }

        auto root = doc.object();
        if (root.contains(QStringLiteral("reconnect_token")))
        {
            auto token = root.value(QStringLiteral("reconnect_token")).toString();
            QMetaObject::invokeMethod(this->manager_, [this, token] {
                this->manager_->setReconnectToken(token);
            });
        }

        if (auto event = StreamElementsManager::parseTipPayload(root))
        {
            QMetaObject::invokeMethod(this->manager_, [this, ev = *event] {
                this->manager_->tipReceived.invoke(ev);
                if (auto *app = getApp())
                {
                    if (auto *twitch = app->getTwitch())
                    {
                        twitch->forEachChannel([&ev](ChannelPtr channel) {
                            if (auto *tc = dynamic_cast<TwitchChannel *>(channel.get()))
                            {
                                tc->addMarqueeEvent(ev);
                            }
                        });
                    }
                }
            });
        }
    }

    void onBinaryMessage(QByteArray /*data*/) override
    {
    }

    void onClose(std::unique_ptr<WebSocketListener> /*self*/) override
    {
        qCDebug(chatterinoWebsocket) << "[StreamElements] Socket closed";
        QMetaObject::invokeMethod(this->manager_, [this] {
            this->manager_->handleClosed();
        });
    }

private:
    StreamElementsManager *const manager_;
};

}  // namespace

StreamElementsManager::StreamElementsManager(QObject *parent)
    : QObject(parent)
{
    this->reconnectTimer_.setSingleShot(true);
    QObject::connect(&this->reconnectTimer_, &QTimer::timeout, this, [this] {
        this->openSocket();
    });

    getSettings()->streamElementsToken.connect(
        [this](const QString &token) {
            if (token.isEmpty())
            {
                this->closeSocket();
            }
            else if (this->isStarted_)
            {
                this->openSocket();
            }
        },
        this->connections_);
}

StreamElementsManager::~StreamElementsManager()
{
    this->stop();
}

void StreamElementsManager::start()
{
    this->isStarted_ = true;
    if (!getSettings()->streamElementsToken.getValue().isEmpty())
    {
        this->openSocket();
    }
}

void StreamElementsManager::stop()
{
    this->isStarted_ = false;
    this->reconnectTimer_.stop();
    this->closeSocket();
}

void StreamElementsManager::reconnect()
{
    this->openSocket();
}

void StreamElementsManager::handleConnected()
{
    this->isConnected_ = true;
    this->sendSubscribe();
}

void StreamElementsManager::handleClosed()
{
    this->isConnected_ = false;
    if (this->isStarted_ &&
        !getSettings()->streamElementsToken.getValue().isEmpty())
    {
        this->reconnectTimer_.start(5000);
    }
}

void StreamElementsManager::setReconnectToken(const QString &token)
{
    this->reconnectToken_ = token;
}

void StreamElementsManager::openSocket()
{
    this->closeSocket();

    auto token = getSettings()->streamElementsToken.getValue().trimmed();
    if (token.isEmpty())
    {
        return;
    }

    QString urlStr = QStringLiteral("wss://astro.streamelements.com/");
    if (!this->reconnectToken_.isEmpty())
    {
        urlStr += QStringLiteral("?reconnect_token=") + this->reconnectToken_;
    }

    WebSocketOptions opts{
        .url = QUrl(urlStr),
        .headers = {},
    };

    auto listener = std::make_unique<StreamElementsListener>(this);
    this->handle_ = this->pool_.createSocket(std::move(opts), std::move(listener));
}

void StreamElementsManager::closeSocket()
{
    this->isConnected_ = false;
    this->handle_.close();
}

void StreamElementsManager::sendSubscribe()
{
    auto token = getSettings()->streamElementsToken.getValue().trimmed();
    if (token.isEmpty())
    {
        return;
    }

    QJsonObject subMsg;
    subMsg[QStringLiteral("type")] = QStringLiteral("subscribe");
    subMsg[QStringLiteral("nonce")] = QString::number(QDateTime::currentMSecsSinceEpoch());

    QJsonObject data;
    data[QStringLiteral("topic")] = QStringLiteral("channel.tips");
    data[QStringLiteral("token")] = token;
    data[QStringLiteral("token_type")] =
        (token.contains('.') || token.length() > 60) ? QStringLiteral("jwt")
                                                     : QStringLiteral("apikey");

    subMsg[QStringLiteral("data")] = data;

    QJsonDocument doc(subMsg);
    this->handle_.sendText(doc.toJson(QJsonDocument::Compact));
}

std::optional<MarqueeEvent> StreamElementsManager::parseTipPayload(const QJsonObject &root)
{
    QString topic = root.value(QStringLiteral("topic")).toString();
    QString type = root.value(QStringLiteral("type")).toString();

    QJsonObject dataObj;
    QString username;
    double amount = 0.0;
    QString currency = QStringLiteral("USD");
    QString userMessage;

    if (topic == QStringLiteral("channel.tips") || type == QStringLiteral("message"))
    {
        dataObj = root.value(QStringLiteral("data")).toObject();
        auto donationObj = dataObj.value(QStringLiteral("donation")).toObject();
        auto userObj = donationObj.value(QStringLiteral("user")).toObject();
        username = userObj.value(QStringLiteral("username")).toString();
        amount = donationObj.value(QStringLiteral("amount")).toDouble();
        currency = donationObj.value(QStringLiteral("currency")).toString(QStringLiteral("USD"));
        userMessage = donationObj.value(QStringLiteral("message")).toString();
    }
    else if (type == QStringLiteral("tip"))
    {
        dataObj = root.value(QStringLiteral("data")).toObject();
        username = dataObj.value(QStringLiteral("username")).toString();
        amount = dataObj.value(QStringLiteral("amount")).toDouble();
        currency = dataObj.value(QStringLiteral("currency")).toString(QStringLiteral("USD"));
        userMessage = dataObj.value(QStringLiteral("message")).toString();
    }
    else
    {
        return std::nullopt;
    }

    if (username.isEmpty() || amount <= 0.0)
    {
        return std::nullopt;
    }

    MarqueeEvent event;
    event.type = MarqueeEvent::Type::Tip;
    event.username = username;
    event.displayName = username;
    event.userColor = QColor(0, 177, 106);  // StreamElements brand green
    event.amountText = QStringLiteral("$%1 %2").arg(amount, 0, 'f', 2).arg(currency);
    event.detailText = QStringLiteral("tipped %1").arg(event.amountText);
    event.userMessage = userMessage;
    event.timestamp = QDateTime::currentDateTime();

    return event;
}

}  // namespace chatterino
