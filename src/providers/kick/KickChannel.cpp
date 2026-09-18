// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickChannel.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/kick/KickManager.hpp"
#include "providers/kick/KickMessageBuilder.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace chatterino {

KickChannel::KickChannel(const QString &name, KickPusherClient &pusherClient,
                         KickEmotes &emotes)
    : Channel(name, Channel::Type::Kick)
    , pusherClient_(pusherClient)
    , emotes_(emotes)
{
    this->connections_.managedConnect(
        this->pusherClient_.messageReceived,
        [this](int64_t targetChatroomId, const KickMessage &msg) {
            if (this->chatroomId_ > 0 && targetChatroomId == this->chatroomId_)
            {
                this->handleKickMessage(msg);
            }
        });

    this->refreshChatroom();
}

KickChannel::~KickChannel()
{
    if (this->chatroomId_ > 0)
    {
        this->pusherClient_.unsubscribeChatroom(this->chatroomId_);
    }
}

int64_t KickChannel::chatroomId() const
{
    return this->chatroomId_;
}

void KickChannel::setChatroomId(int64_t id)
{
    if (this->chatroomId_ == id)
    {
        return;
    }

    if (this->chatroomId_ > 0)
    {
        this->pusherClient_.unsubscribeChatroom(this->chatroomId_);
    }

    this->chatroomId_ = id;
    this->subscribe();
}

void KickChannel::refreshChatroom()
{
    QString rawName = this->getName().trimmed();
    if (rawName.startsWith(QStringLiteral("kick:"), Qt::CaseInsensitive))
    {
        rawName = rawName.mid(5).trimmed();
    }

    // Check if format is slug:id
    if (rawName.contains(u':'))
    {
        auto parts = rawName.split(u':');
        bool ok = false;
        int64_t parsedId = parts.last().toLongLong(&ok);
        if (ok && parsedId > 0)
        {
            this->setChatroomId(parsedId);
            return;
        }
    }

    // Check if name is purely numeric chatroom ID
    bool ok = false;
    int64_t parsedId = rawName.toLongLong(&ok);
    if (ok && parsedId > 0)
    {
        this->setChatroomId(parsedId);
        return;
    }

    this->resolveChatroomId();
}

void KickChannel::resolveChatroomId()
{
    QString slug = this->getName().trimmed();
    if (slug.startsWith(QStringLiteral("kick:"), Qt::CaseInsensitive))
    {
        slug = slug.mid(5).trimmed();
    }

    auto url = QStringLiteral("https://kick.com/api/v2/channels/%1").arg(slug);

    NetworkRequest(QUrl(url))
        .header("Accept", "application/json")
        .header("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .timeout(10000)
        .onSuccess([this, slug](NetworkResult result) {
            auto root = result.parseJson();
            int64_t id = root.value(QStringLiteral("chatroom"))
                             .toObject()
                             .value(QStringLiteral("id"))
                             .toVariant()
                             .toLongLong();
            if (id <= 0)
            {
                id = root.value(QStringLiteral("id")).toVariant().toLongLong();
            }
            if (id > 0)
            {
                this->setChatroomId(id);
                this->addMessage(makeSystemMessage(
                    QStringLiteral("Connected to Kick chat for %1 (chatroom %2)").arg(slug).arg(id)),
                    MessageContext::Original);
            }
            else
            {
                this->addMessage(makeSystemMessage(
                    QStringLiteral("Could not find chatroom ID for Kick channel '%1'.").arg(slug)),
                    MessageContext::Original);
            }
        })
        .onError([this, slug](NetworkResult /*result*/) {
            this->addMessage(makeSystemMessage(
                QStringLiteral("Failed to resolve Kick channel '%1'. You can join using numeric chatroom ID (e.g. kick:%1:<chatroom_id>).").arg(slug)),
                MessageContext::Original);
        })
        .execute();
}

void KickChannel::subscribe()
{
    if (this->chatroomId_ <= 0)
    {
        return;
    }

    this->pusherClient_.subscribeChatroom(this->chatroomId_);
}

void KickChannel::handleKickMessage(const KickMessage &msg)
{
    auto msgPtr = KickMessageBuilder::build(msg, this->emotes_, false);
    this->addMessage(msgPtr, MessageContext::Original);
}

QString KickChannel::channelSlug() const
{
    QString rawName = this->getName().trimmed();
    if (rawName.startsWith(QStringLiteral("kick:"), Qt::CaseInsensitive))
    {
        rawName = rawName.mid(5).trimmed();
    }
    if (rawName.contains(u':'))
    {
        rawName = rawName.split(u':').first().trimmed();
    }
    return rawName.toLower();
}

bool KickChannel::canSendMessage() const
{
    return this->chatroomId_ > 0 && getApp()->getKick()->hasAccount();
}

bool KickChannel::isBroadcaster() const
{
    auto currentUser = getApp()->getKick()->getCurrentUsername();
    return !currentUser.isEmpty() &&
           currentUser.compare(this->channelSlug(), Qt::CaseInsensitive) == 0;
}

bool KickChannel::isMod() const
{
    if (this->isBroadcaster())
    {
        return true;
    }

    auto currentUser = getApp()->getKick()->getCurrentUsername();
    if (currentUser.isEmpty())
    {
        return false;
    }

    for (const auto &msg : this->getMessageSnapshot())
    {
        if (msg->loginName.compare(currentUser, Qt::CaseInsensitive) == 0 ||
            msg->displayName.compare(currentUser, Qt::CaseInsensitive) == 0)
        {
            for (const auto &badge : msg->externalBadges)
            {
                if (badge == QStringLiteral("kick:moderator") ||
                    badge == QStringLiteral("kick:broadcaster"))
                {
                    return true;
                }
            }
        }

    }

    return false;
}

bool KickChannel::hasModRights() const
{
    return this->isMod() || this->isBroadcaster() ||
           getApp()->getKick()->hasAccount();
}

void KickChannel::sendMessage(const QString &message)
{
    QString text = message.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    auto *kickMgr = getApp()->getKick();
    if (!kickMgr || !kickMgr->hasAccount())
    {
        this->addMessage(
            makeSystemMessage(
                QStringLiteral("You must be logged into Kick to send messages or "
                               "moderate. Add your Kick account in Settings -> Accounts.")),
            MessageContext::Original);
        return;
    }

    if (this->chatroomId_ <= 0)
    {
        this->addMessage(
            makeSystemMessage(
                QStringLiteral("Kick chatroom ID has not been resolved yet. Please wait.")),
            MessageContext::Original);
        return;
    }

    // Handle moderation slash commands
    if (text.startsWith(QStringLiteral("/ban "), Qt::CaseInsensitive))
    {
        auto target = text.mid(5).trimmed().split(u' ').first();
        kickMgr->banUser(this->channelSlug(), target,
                         [this, target](bool ok, QString error) {
                             if (ok)
                             {
                                 this->addMessage(
                                     makeSystemMessage(
                                         QStringLiteral("Successfully banned %1 on Kick.")
                                             .arg(target)),
                                     MessageContext::Original);
                             }
                             else
                             {
                                 this->addMessage(
                                     makeSystemMessage(
                                         QStringLiteral("Kick ban error: %1")
                                             .arg(error)),
                                     MessageContext::Original);
                             }
                         });
    }
    else if (text.startsWith(QStringLiteral("/timeout "), Qt::CaseInsensitive))
    {
        auto parts = text.mid(9).trimmed().split(u' ', Qt::SkipEmptyParts);
        if (!parts.isEmpty())
        {
            auto target = parts.first();
            int duration = 300;
            if (parts.size() > 1)
            {
                bool ok = false;
                int d = parts[1].toInt(&ok);
                if (ok && d > 0)
                {
                    duration = d;
                }
            }

            kickMgr->timeoutUser(this->channelSlug(), target, duration,
                                 [this, target, duration](bool ok, QString error) {
                                     if (ok)
                                     {
                                         this->addMessage(
                                             makeSystemMessage(
                                                 QStringLiteral(
                                                     "Successfully timed out %1 for %2s on Kick.")
                                                     .arg(target)
                                                     .arg(duration)),
                                             MessageContext::Original);
                                     }
                                     else
                                     {
                                         this->addMessage(
                                             makeSystemMessage(
                                                 QStringLiteral("Kick timeout error: %1")
                                                     .arg(error)),
                                             MessageContext::Original);
                                     }
                                 });
        }
    }
    else if (text.startsWith(QStringLiteral("/unban "), Qt::CaseInsensitive))
    {
        auto target = text.mid(7).trimmed().split(u' ').first();
        kickMgr->unbanUser(this->channelSlug(), target,
                           [this, target](bool ok, QString error) {
                               if (ok)
                               {
                                   this->addMessage(
                                       makeSystemMessage(
                                           QStringLiteral("Successfully unbanned %1 on Kick.")
                                               .arg(target)),
                                       MessageContext::Original);
                               }
                               else
                               {
                                   this->addMessage(
                                       makeSystemMessage(
                                           QStringLiteral("Kick unban error: %1")
                                               .arg(error)),
                                       MessageContext::Original);
                               }
                           });
    }

    // Always send chat message (or chat command) via Kick messages endpoint
    auto url = QStringLiteral("https://kick.com/api/v2/messages/send/%1")
                   .arg(this->chatroomId_);
    QJsonObject json;
    json[QStringLiteral("content")] = text;
    json[QStringLiteral("type")] = QStringLiteral("message");

    NetworkRequest(QUrl(url), NetworkRequestType::Post)
        .header("Accept", "application/json")
        .header("Content-Type", "application/json")
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(kickMgr->getAuthToken()))
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .json(json)
        .timeout(10000)
        .onSuccess([](NetworkResult /*res*/) {
            // Message sent successfully; Pusher WebSocket delivers the broadcasted message.
        })
        .onError([this](NetworkResult res) {
            QString errMsg =
                QStringLiteral("Failed to send Kick message (HTTP %1): %2")
                    .arg(res.status().value_or(0))
                    .arg(QString::fromUtf8(res.getData()));

            this->addMessage(makeSystemMessage(errMsg),
                             MessageContext::Original);
        })
        .execute();
}

}  // namespace chatterino

