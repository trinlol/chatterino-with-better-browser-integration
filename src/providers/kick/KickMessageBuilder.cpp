// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickMessageBuilder.hpp"

#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/MessageColor.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageThread.hpp"

#include <QPixmap>
#include <QStringBuilder>

namespace chatterino {

MessagePtr KickMessageBuilder::build(const KickMessage &msg, KickEmotes &emotes,
                                     bool includePlatformBadge)
{
    auto message = std::make_shared<Message>();
    message->id = msg.id;
    message->serverReceivedTime = msg.createdAt.isValid() ? msg.createdAt : QDateTime::currentDateTime();
    message->loginName = msg.senderSlug.isEmpty() ? msg.senderUsername.toLower() : msg.senderSlug.toLower();
    message->displayName = msg.senderUsername;
    message->userID = msg.senderId;
    message->messageText = msg.content;

    // Badges tracking on message
    for (const auto &badge : msg.badges)
    {
        message->externalBadges.append(QStringLiteral("kick:%1").arg(badge.type));
    }

    // Timestamp
    auto time = message->serverReceivedTime.time();
    message->elements.push_back(std::make_unique<TimestampElement>(time));

    // Platform Badge (Kick)
    if (includePlatformBadge)
    {
        static auto badgeEmote = []() {
            auto badgeImg = Image::fromResourcePixmap(QPixmap(QStringLiteral(":/buttons/kick.svg")), 1);
            return std::make_shared<Emote>(Emote{
                .name = EmoteName{QStringLiteral("[K]")},
                .images = ImageSet{badgeImg},
                .tooltip = Tooltip{QStringLiteral("Kick Live Stream")},
                .homePage = Url{QStringLiteral("https://kick.com")},
                .zeroWidth = false,
                .id = EmoteId{QStringLiteral("kick_badge")},
                .author = EmoteAuthor{},
            });
        }();

        auto badgeElem = std::make_unique<BadgeElement>(badgeEmote, MessageElementFlag::BadgeSharedChannel);
        badgeElem->setTrailingSpace(true);
        message->elements.push_back(std::move(badgeElem));
    }

    // Badges (Moderator, Broadcaster, Subscriber, etc.)
    for (const auto &badge : msg.badges)
    {
        QString badgeText;
        MessageColor badgeColor = MessageColor::System;
        if (badge.type == QStringLiteral("broadcaster"))
        {
            badgeText = QStringLiteral("[STREAMER]");
            badgeColor = QColor(0xE9, 0x1E, 0x63);
        }
        else if (badge.type == QStringLiteral("moderator"))
        {
            badgeText = QStringLiteral("[MOD]");
            badgeColor = QColor(0x53, 0xFC, 0x18);
        }
        else if (badge.type == QStringLiteral("subscriber"))
        {
            badgeText = badge.count > 0 ? QStringLiteral("[SUB %1m]").arg(badge.count)
                                        : QStringLiteral("[SUB]");
            badgeColor = QColor(0x00, 0xBC, 0xD4);
        }
        else if (badge.type == QStringLiteral("vip"))
        {
            badgeText = QStringLiteral("[VIP]");
            badgeColor = QColor(0xE0, 0x40, 0xFB);
        }
        else if (!badge.text.isEmpty())
        {
            badgeText = QStringLiteral("[%1]").arg(badge.text.toUpper());
        }

        if (!badgeText.isEmpty())
        {
            auto el = std::make_unique<TextElement>(badgeText, MessageElementFlag::Badges,
                                                   badgeColor, FontStyle::ChatMediumBold);
            el->setTrailingSpace(true);
            message->elements.push_back(std::move(el));
        }
    }

    // Username
    QColor usernameColor = msg.senderColor.isValid() ? msg.senderColor : QColor(0x53, 0xFC, 0x18);
    auto usernameElem = std::make_unique<TextElement>(
        msg.senderUsername + QStringLiteral(":"), MessageElementFlag::Username,
        usernameColor, FontStyle::ChatMediumBold);
    usernameElem->setTrailingSpace(true);
    QString userIdentifier = msg.senderSlug.isEmpty() ? msg.senderUsername : msg.senderSlug;
    usernameElem->setLink({Link::UserInfo, userIdentifier});
    usernameElem->setTooltip(QStringLiteral("%1 on Kick (click for user card)").arg(msg.senderUsername));
    message->elements.push_back(std::move(usernameElem));


    // Message content with Kick emote parsing
    const auto &regex = KickEmotes::emoteRegex();
    int lastPos = 0;
    auto it = regex.globalMatch(msg.content);
    while (it.hasNext())
    {
        auto match = it.next();
        int matchPos = match.capturedStart();
        if (matchPos > lastPos)
        {
            QString textPart = msg.content.mid(lastPos, matchPos - lastPos);
            auto textElem = std::make_unique<TextElement>(textPart, MessageElementFlag::Text,
                                                          MessageColor::Text, FontStyle::ChatMedium);
            textElem->setTrailingSpace(false);
            message->elements.push_back(std::move(textElem));
        }

        QString emoteId = match.captured(1);
        QString emoteName = match.captured(2);
        auto emotePtr = emotes.getOrCreateEmote(emoteId, emoteName);
        auto emoteElem = std::make_unique<EmoteElement>(emotePtr, MessageElementFlag::Emote, MessageColor::Text);
        emoteElem->setTrailingSpace(true);
        message->elements.push_back(std::move(emoteElem));

        lastPos = match.capturedEnd();
    }

    if (lastPos < msg.content.length())
    {
        QString remainingText = msg.content.mid(lastPos);
        auto textElem = std::make_unique<TextElement>(remainingText, MessageElementFlag::Text,
                                                      MessageColor::Text, FontStyle::ChatMedium);
        textElem->setTrailingSpace(true);
        message->elements.push_back(std::move(textElem));
    }

    message->searchText = QStringLiteral("kick %1: %2")
                              .arg(msg.senderUsername, msg.content);

    return message;
}

}  // namespace chatterino
