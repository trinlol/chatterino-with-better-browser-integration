// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <deque>
#include <mutex>

namespace chatterino {

class CombinedChannel final : public Channel
{
public:
    CombinedChannel(const QString &name, ChannelPtr twitchChannel,
                    ChannelPtr kickChannel);
    ~CombinedChannel() override;

    ChannelPtr twitchChannel() const;
    ChannelPtr kickChannel() const;

    const QString &getLocalizedName() const override;

    bool canSendMessage() const override;
    bool isWritable() const override;
    void sendMessage(const QString &message) override;

    bool isMod() const override;
    bool isBroadcaster() const override;
    bool hasModRights() const override;
    bool isLive() const override;
    void reconnect() override;

private:
    QString displayName_;
    struct RecentMessage {
        QString author;
        QString text;
        QString platform;
        qint64 timestampMs{0};
    };

    bool isRepeatSpam(const QString &author, const QString &text,
                      const QString &platform);
    void handleIncomingMessage(const MessagePtr &msg, const QString &platform);

    ChannelPtr twitchChannel_;
    ChannelPtr kickChannel_;

    pajlada::Signals::SignalHolder connections_;

    std::mutex historyMutex_;
    std::deque<RecentMessage> recentMessages_;
};

}  // namespace chatterino
