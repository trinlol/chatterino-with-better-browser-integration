// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "providers/kick/KickMessage.hpp"

#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <memory>

namespace chatterino {

class KickPusherClient final : public QObject
{
    Q_OBJECT

public:
    explicit KickPusherClient(QObject *parent = nullptr);
    ~KickPusherClient() override;

    void subscribeChatroom(int64_t chatroomId);
    void unsubscribeChatroom(int64_t chatroomId);

    pajlada::Signals::Signal<int64_t, const KickMessage &> messageReceived;
    pajlada::Signals::Signal<bool> connectionStateChanged;

    bool isConnected() const;

    void handleConnected();
    void handleClosed();
    void handleTextMessage(const QString &text);

private:
    void fetchConnectionTokenAndConnect();
    void openSocket();
    void closeSocket();
    void sendSubscribe(int64_t chatroomId);

    WebSocketPool pool_{QStringLiteral("KickRealtime")};
    WebSocketHandle handle_;
    pajlada::Signals::SignalHolder connections_;

    QSet<int64_t> subscribedChatrooms_;
    QTimer reconnectTimer_;

    QString connectionToken_;
    bool isFetchingToken_{false};
    bool isConnected_{false};
    bool isHandshakeComplete_{false};
    int64_t nextCmdId_{0};
};

}  // namespace chatterino
