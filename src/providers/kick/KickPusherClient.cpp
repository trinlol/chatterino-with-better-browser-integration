// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickPusherClient.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "providers/kick/KickManager.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QUuid>

namespace chatterino {

namespace {

class KickWebSocketListener : public WebSocketListener
{
public:
    explicit KickWebSocketListener(KickPusherClient *client)
        : client_(client)
    {
    }

    void onOpen() override
    {
        qCDebug(chatterinoWebsocket) << "[KickRealtime] Connected to Centrifugo WebSocket";
        QMetaObject::invokeMethod(this->client_, [this] {
            this->client_->handleConnected();
        });
    }

    void onTextMessage(QByteArray data) override
    {
        QString text = QString::fromUtf8(data);
        QMetaObject::invokeMethod(this->client_, [this, text] {
            this->client_->handleTextMessage(text);
        });
    }

    void onBinaryMessage(QByteArray /*data*/) override
    {
    }

    void onClose(std::unique_ptr<WebSocketListener> /*self*/) override
    {
        qCDebug(chatterinoWebsocket) << "[KickRealtime] Socket closed";
        QMetaObject::invokeMethod(this->client_, [this] {
            this->client_->handleClosed();
        });
    }

private:
    KickPusherClient *const client_;
};

}  // namespace

KickPusherClient::KickPusherClient(QObject *parent)
    : QObject(parent)
{
    this->reconnectTimer_.setSingleShot(true);
    QObject::connect(&this->reconnectTimer_, &QTimer::timeout, this, [this] {
        this->fetchConnectionTokenAndConnect();
    });
}

KickPusherClient::~KickPusherClient()
{
    this->closeSocket();
}

bool KickPusherClient::isConnected() const
{
    return this->isConnected_;
}

void KickPusherClient::subscribeChatroom(int64_t chatroomId)
{
    if (chatroomId <= 0)
    {
        return;
    }

    this->subscribedChatrooms_.insert(chatroomId);

    if (this->isConnected_ && this->isHandshakeComplete_)
    {
        this->sendSubscribe(chatroomId);
    }
    else if (!this->isConnected_ && !this->isFetchingToken_)
    {
        this->fetchConnectionTokenAndConnect();
    }
}

void KickPusherClient::unsubscribeChatroom(int64_t chatroomId)
{
    this->subscribedChatrooms_.remove(chatroomId);
    if (this->isConnected_ && this->isHandshakeComplete_)
    {
        this->nextCmdId_++;
        QJsonObject root;
        root[QStringLiteral("id")] = this->nextCmdId_;
        QJsonObject data;
        data[QStringLiteral("channel")] = QStringLiteral("chatrooms.%1.v2").arg(chatroomId);
        root[QStringLiteral("unsubscribe")] = data;
        this->handle_.sendText(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    if (this->subscribedChatrooms_.isEmpty())
    {
        this->closeSocket();
    }
}

void KickPusherClient::fetchConnectionTokenAndConnect()
{
    if (this->subscribedChatrooms_.isEmpty() || this->isFetchingToken_)
    {
        return;
    }

    this->isFetchingToken_ = true;

    QString clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject body;
    body[QStringLiteral("client_id")] = clientId;

    // Prepare auth header value if logged in
    auto *kickMgr = getApp()->getKick();
    QString authHeader;
    if (kickMgr && kickMgr->hasAccount() && !kickMgr->getAuthToken().isEmpty())
    {
        authHeader = QStringLiteral("Bearer %1").arg(kickMgr->getAuthToken());
    }

    auto makeReq = [&]() {
        auto r = NetworkRequest(
            QUrl(QStringLiteral("https://web.kick.com/api/v1/realtime/auth/connection")),
            NetworkRequestType::Post);
        if (!authHeader.isEmpty())
        {
            return std::move(r)
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("x-app-platform", "web")
                .header("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
                .header("Authorization", authHeader)
                .json(body)
                .timeout(10000);
        }
        return std::move(r)
            .header("Content-Type", "application/json")
            .header("Accept", "application/json")
            .header("x-app-platform", "web")
            .header("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
            .json(body)
            .timeout(10000);
    };

    std::move(makeReq())
        .onSuccess([this](NetworkResult res) {
            this->isFetchingToken_ = false;
            auto json = res.parseJson();
            this->connectionToken_ = json.value(QStringLiteral("data"))
                                         .toObject()
                                         .value(QStringLiteral("token"))
                                         .toString();
            if (!this->connectionToken_.isEmpty())
            {
                this->openSocket();
            }
            else
            {
                this->reconnectTimer_.start(4000);
            }
        })
        .onError([this](NetworkResult /*res*/) {
            this->isFetchingToken_ = false;
            this->reconnectTimer_.start(4000);
        })
        .execute();
}

void KickPusherClient::openSocket()
{
    this->closeSocket();

    if (this->subscribedChatrooms_.isEmpty() || this->connectionToken_.isEmpty())
    {
        return;
    }

    QString urlStr = QStringLiteral(
        "wss://realtime.us-west-2.platform.kick.com/connection/websocket");

    WebSocketOptions opts{
        .url = QUrl(urlStr),
        .headers = {},
    };

    this->handle_ = this->pool_.createSocket(
        std::move(opts), std::make_unique<KickWebSocketListener>(this));
}

void KickPusherClient::closeSocket()
{
    this->handle_.close();
    this->isConnected_ = false;
    this->isHandshakeComplete_ = false;
}

void KickPusherClient::handleConnected()
{
    this->isConnected_ = true;
    this->isHandshakeComplete_ = false;
    this->nextCmdId_ = 1;

    // Send Centrifugo connect frame with token
    QJsonObject connectCmd;
    connectCmd[QStringLiteral("id")] = this->nextCmdId_;
    QJsonObject connectData;
    connectData[QStringLiteral("token")] = this->connectionToken_;
    connectCmd[QStringLiteral("connect")] = connectData;

    this->handle_.sendText(QJsonDocument(connectCmd).toJson(QJsonDocument::Compact));
}

void KickPusherClient::handleClosed()
{
    this->isConnected_ = false;
    this->isHandshakeComplete_ = false;
    this->connectionToken_ = QString();
    this->connectionStateChanged.invoke(false);

    if (!this->subscribedChatrooms_.isEmpty())
    {
        this->reconnectTimer_.start(4000);
    }
}

void KickPusherClient::sendSubscribe(int64_t chatroomId)
{
    this->nextCmdId_++;
    QJsonObject subCmd;
    subCmd[QStringLiteral("id")] = this->nextCmdId_;
    QJsonObject subData;
    subData[QStringLiteral("channel")] = QStringLiteral("chatrooms.%1.v2").arg(chatroomId);
    subCmd[QStringLiteral("subscribe")] = subData;

    this->handle_.sendText(QJsonDocument(subCmd).toJson(QJsonDocument::Compact));
}

void KickPusherClient::handleTextMessage(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("{}"))
    {
        // Centrifugo ping frame: respond with pong
        this->handle_.sendText(QByteArrayLiteral("{}"));
        return;
    }

    auto doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject())
    {
        return;
    }

    auto root = doc.object();

    if (root.contains(QStringLiteral("connect")))
    {
        this->isHandshakeComplete_ = true;
        this->connectionStateChanged.invoke(true);
        for (int64_t chatroomId : this->subscribedChatrooms_)
        {
            this->sendSubscribe(chatroomId);
        }
        return;
    }

    if (root.contains(QStringLiteral("push")))
    {
        auto pushObj = root.value(QStringLiteral("push")).toObject();
        auto pubObj = pushObj.value(QStringLiteral("pub")).toObject();
        auto dataVal = pubObj.value(QStringLiteral("data"));

        QJsonObject eventContainer;
        if (dataVal.isString())
        {
            auto parsed = QJsonDocument::fromJson(dataVal.toString().toUtf8());
            if (parsed.isObject())
            {
                eventContainer = parsed.object();
            }
        }
        else if (dataVal.isObject())
        {
            eventContainer = dataVal.toObject();
        }

        if (eventContainer.isEmpty())
        {
            return;
        }

        QJsonObject dataObj;
        QJsonValue innerData = eventContainer.value(QStringLiteral("data"));
        if (innerData.isString())
        {
            auto innerDoc = QJsonDocument::fromJson(innerData.toString().toUtf8());
            if (innerDoc.isObject())
            {
                dataObj = innerDoc.object();
            }
        }
        else if (innerData.isObject())
        {
            dataObj = innerData.toObject();
        }
        else if (eventContainer.contains(QStringLiteral("content")) &&
                 eventContainer.contains(QStringLiteral("sender")))
        {
            dataObj = eventContainer;
        }

        if (dataObj.isEmpty())
        {
            return;
        }

        KickMessage msg;
        msg.id = dataObj.value(QStringLiteral("id")).toString();
        msg.chatroomId = dataObj.value(QStringLiteral("chatroom_id")).toVariant().toLongLong();
        msg.content = dataObj.value(QStringLiteral("content")).toString();

        QString createdAtStr = dataObj.value(QStringLiteral("created_at")).toString();
        if (!createdAtStr.isEmpty())
        {
            msg.createdAt = QDateTime::fromString(createdAtStr, Qt::ISODate);
        }

        QJsonObject senderObj = dataObj.value(QStringLiteral("sender")).toObject();
        msg.senderId = senderObj.value(QStringLiteral("id")).toVariant().toString();
        msg.senderUsername = senderObj.value(QStringLiteral("username")).toString();
        msg.senderSlug = senderObj.value(QStringLiteral("slug")).toString();

        QJsonObject identityObj = senderObj.value(QStringLiteral("identity")).toObject();
        QString colorStr = identityObj.value(QStringLiteral("color")).toString();
        if (!colorStr.isEmpty())
        {
            msg.senderColor = QColor(colorStr);
        }

        QJsonArray badgesArr = identityObj.value(QStringLiteral("badges")).toArray();
        for (const auto &badgeVal : badgesArr)
        {
            QJsonObject bObj = badgeVal.toObject();
            KickBadge kb;
            kb.type = bObj.value(QStringLiteral("type")).toString();
            kb.text = bObj.value(QStringLiteral("text")).toString();
            kb.count = bObj.value(QStringLiteral("count")).toInt();
            msg.badges.push_back(std::move(kb));
        }

        this->messageReceived.invoke(msg.chatroomId, msg);
    }
}

}  // namespace chatterino
