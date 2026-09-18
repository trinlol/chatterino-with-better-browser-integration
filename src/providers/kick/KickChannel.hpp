// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"
#include "providers/kick/KickEmotes.hpp"
#include "providers/kick/KickMessage.hpp"
#include "providers/kick/KickPusherClient.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QObject>

namespace chatterino {

class KickChannel final : public Channel
{
public:
    KickChannel(const QString &name, KickPusherClient &pusherClient,
                KickEmotes &emotes);
    ~KickChannel() override;

    int64_t chatroomId() const;
    void setChatroomId(int64_t id);
    QString channelSlug() const;

    void refreshChatroom();

    void sendMessage(const QString &message) override;
    bool canSendMessage() const override;
    bool isMod() const override;
    bool isBroadcaster() const override;
    bool hasModRights() const override;

private:
    void resolveChatroomId();

    void subscribe();
    void handleKickMessage(const KickMessage &msg);

    KickPusherClient &pusherClient_;
    KickEmotes &emotes_;

    int64_t chatroomId_{0};
    bool isSubscribed_{false};
    pajlada::Signals::SignalHolder connections_;
};

}  // namespace chatterino
