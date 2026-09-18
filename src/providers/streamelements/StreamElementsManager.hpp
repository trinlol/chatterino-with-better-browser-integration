// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "providers/twitch/MarqueeEvent.hpp"

#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

namespace chatterino {

class StreamElementsManager final : public QObject
{
    Q_OBJECT

public:
    explicit StreamElementsManager(QObject *parent = nullptr);
    ~StreamElementsManager() override;

    void start();
    void stop();
    void reconnect();

    static std::optional<MarqueeEvent> parseTipPayload(const QJsonObject &root);

    pajlada::Signals::Signal<const MarqueeEvent &> tipReceived;

    void handleConnected();
    void handleClosed();
    void setReconnectToken(const QString &token);

private:
    void openSocket();
    void closeSocket();
    void sendSubscribe();

    WebSocketPool pool_{QStringLiteral("StreamElements")};
    WebSocketHandle handle_;

    pajlada::Signals::SignalHolder connections_;
    QTimer reconnectTimer_;

    QString reconnectToken_;
    bool isConnected_{false};
    bool isStarted_{false};
};

}  // namespace chatterino
