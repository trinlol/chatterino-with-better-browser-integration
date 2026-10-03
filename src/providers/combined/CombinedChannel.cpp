// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/combined/CombinedChannel.hpp"

#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "singletons/Settings.hpp"

#include <QDateTime>
#include <QPixmap>

namespace chatterino {

CombinedChannel::CombinedChannel(const QString &name, ChannelPtr twitchChannel,
                                 ChannelPtr kickChannel)
    : Channel(name, Channel::Type::Combined)
    , twitchChannel_(std::move(twitchChannel))
    , kickChannel_(std::move(kickChannel))
{
    if (this->twitchChannel_)
    {
        this->connections_.managedConnect(
            this->twitchChannel_->messageAppended,
            [this](MessagePtr &msg, auto /*overridingFlags*/) {
                this->handleIncomingMessage(msg, QStringLiteral("twitch"));
            });
    }

    if (this->kickChannel_)
    {
        this->connections_.managedConnect(
            this->kickChannel_->messageAppended,
            [this](MessagePtr &msg, auto /*overridingFlags*/) {
                this->handleIncomingMessage(msg, QStringLiteral("kick"));
            });
    }
    if (this->twitchChannel_ && this->kickChannel_)
    {
        if (this->twitchChannel_->getName().compare(
                this->kickChannel_->getName(), Qt::CaseInsensitive) == 0)
        {
            this->displayName_ = this->twitchChannel_->getName();
        }
        else
        {
            this->displayName_ = QStringLiteral("%1 + %2").arg(
                this->twitchChannel_->getName(), this->kickChannel_->getName());
        }
    }
    else if (this->twitchChannel_)
    {
        this->displayName_ = this->twitchChannel_->getName();
    }
    else if (this->kickChannel_)
    {
        this->displayName_ = this->kickChannel_->getName();
    }
    else
    {
        this->displayName_ = name;
    }
}

CombinedChannel::~CombinedChannel() = default;

ChannelPtr CombinedChannel::twitchChannel() const
{
    return this->twitchChannel_;
}

ChannelPtr CombinedChannel::kickChannel() const
{
    return this->kickChannel_;
}

const QString &CombinedChannel::getLocalizedName() const
{
    if (!this->displayName_.isEmpty())
    {
        return this->displayName_;
    }
    return this->getName();
}

bool CombinedChannel::canSendMessage() const
{
    if (this->twitchChannel_ && this->twitchChannel_->canSendMessage())
    {
        return true;
    }
    if (this->kickChannel_ && this->kickChannel_->canSendMessage())
    {
        return true;
    }
    return false;
}

bool CombinedChannel::isWritable() const
{
    return true;
}

void CombinedChannel::sendMessage(const QString &message)
{
    QString text = message.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    // Explicit Kick routing: /kick <message> or /k <message>
    if (text.startsWith(QStringLiteral("/kick "), Qt::CaseInsensitive))
    {
        if (this->kickChannel_)
        {
            this->kickChannel_->sendMessage(text.mid(6).trimmed());
        }
        return;
    }
    if (text.startsWith(QStringLiteral("/k "), Qt::CaseInsensitive))
    {
        if (this->kickChannel_)
        {
            this->kickChannel_->sendMessage(text.mid(3).trimmed());
        }
        return;
    }

    // Explicit Twitch routing: /twitch <message>
    if (text.startsWith(QStringLiteral("/twitch "), Qt::CaseInsensitive))
    {
        if (this->twitchChannel_)
        {
            this->twitchChannel_->sendMessage(text.mid(8).trimmed());
        }
        return;
    }

    // Send as Twitch by default
    if (this->twitchChannel_)
    {
        this->twitchChannel_->sendMessage(text);
    }
    else if (this->kickChannel_)
    {
        this->kickChannel_->sendMessage(text);
    }
}

bool CombinedChannel::isMod() const
{
    return this->twitchChannel_ && this->twitchChannel_->isMod();
}

bool CombinedChannel::isBroadcaster() const
{
    return this->twitchChannel_ && this->twitchChannel_->isBroadcaster();
}

bool CombinedChannel::hasModRights() const
{
    return this->twitchChannel_ && this->twitchChannel_->hasModRights();
}

bool CombinedChannel::isLive() const
{
    return (this->twitchChannel_ && this->twitchChannel_->isLive()) ||
           (this->kickChannel_ && this->kickChannel_->isLive());
}

void CombinedChannel::reconnect()
{
    if (this->twitchChannel_)
    {
        this->twitchChannel_->reconnect();
    }
    if (this->kickChannel_)
    {
        this->kickChannel_->reconnect();
    }
}

bool CombinedChannel::isRepeatSpam(const QString &author, const QString &text,
                                   const QString &platform)
{
    if (!getSettings()->kickAntiSpamFilter.getValue())
    {
        return false;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 windowMs = getSettings()->kickAntiSpamWindowSeconds.getValue() * 1000LL;

    std::lock_guard<std::mutex> lock(this->historyMutex_);

    // Purge old messages
    while (!this->recentMessages_.empty() &&
           now - this->recentMessages_.front().timestampMs > windowMs)
    {
        this->recentMessages_.pop_front();
    }

    QString normAuthor = author.trimmed().toLower();
    QString normText = text.trimmed().toLower();

    for (const auto &item : this->recentMessages_)
    {
        if (item.platform != platform &&
            item.author == normAuthor &&
            item.text == normText)
        {
            // Repeat cross-platform duplicate detected!
            return true;
        }
    }

    this->recentMessages_.push_back(RecentMessage{
        .author = normAuthor,
        .text = normText,
        .platform = platform,
        .timestampMs = now,
    });

    return false;
}

void CombinedChannel::handleIncomingMessage(const MessagePtr &msg,
                                            const QString &platform)
{
    if (!msg)
    {
        return;
    }

    if (this->isRepeatSpam(msg->loginName, msg->messageText, platform))
    {
        // Suppress repeat cross-platform spam
        return;
    }

    auto combinedMsg = std::make_shared<Message>();
    combinedMsg->id = msg->id;
    combinedMsg->messageText = msg->messageText;
    combinedMsg->searchText = msg->searchText;
    combinedMsg->loginName = msg->loginName;
    combinedMsg->displayName = msg->displayName;
    combinedMsg->serverReceivedTime = msg->serverReceivedTime;
    combinedMsg->flags = msg->flags;

    // Prepend platform badge
    if (platform == QStringLiteral("twitch"))
    {
        static auto twitchBadge = []() {
            auto img = Image::fromResourcePixmap(QPixmap(QStringLiteral(":/buttons/twitch.svg")), 1);
            return std::make_shared<Emote>(Emote{
                .name = EmoteName{QStringLiteral("[Twitch]")},
                .images = ImageSet{img},
                .tooltip = Tooltip{QStringLiteral("Twitch Chat")},
                .homePage = Url{QStringLiteral("https://twitch.tv")},
                .zeroWidth = false,
                .id = EmoteId{QStringLiteral("twitch_platform_badge")},
                .author = EmoteAuthor{},
            });
        }();

        auto el = std::make_unique<BadgeElement>(twitchBadge, MessageElementFlag::BadgeSharedChannel);
        el->setTrailingSpace(true);
        combinedMsg->elements.push_back(std::move(el));
    }
    else if (platform == QStringLiteral("kick"))
    {
        static auto kickBadge = []() {
            auto img = Image::fromResourcePixmap(QPixmap(QStringLiteral(":/buttons/kick.svg")), 1);
            return std::make_shared<Emote>(Emote{
                .name = EmoteName{QStringLiteral("[Kick]")},
                .images = ImageSet{img},
                .tooltip = Tooltip{QStringLiteral("Kick Chat")},
                .homePage = Url{QStringLiteral("https://kick.com")},
                .zeroWidth = false,
                .id = EmoteId{QStringLiteral("kick_platform_badge")},
                .author = EmoteAuthor{},
            });
        }();

        auto el = std::make_unique<BadgeElement>(kickBadge, MessageElementFlag::BadgeSharedChannel);
        el->setTrailingSpace(true);
        combinedMsg->elements.push_back(std::move(el));
    }

    // Clone remaining elements
    for (const auto &elem : msg->elements)
    {
        combinedMsg->elements.push_back(elem->clone());
    }

    this->addMessage(combinedMsg, MessageContext::Original);
}

}  // namespace chatterino
