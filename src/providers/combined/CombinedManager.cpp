// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/combined/CombinedManager.hpp"

#include "Application.hpp"
#include "providers/kick/KickManager.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"

namespace chatterino {

CombinedManager::CombinedManager(QObject *parent)
    : QObject(parent)
{
}

CombinedManager::~CombinedManager() = default;

ChannelPtr CombinedManager::getOrAddChannel(const QString &channelName)
{
    QString cleanName = channelName.trimmed();
    if (cleanName.startsWith(QStringLiteral("combined:"), Qt::CaseInsensitive))
    {
        cleanName = cleanName.mid(9).trimmed();
    }

    if (cleanName.isEmpty())
    {
        return Channel::getEmpty();
    }

    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = this->channels_.find(cleanName.toLower());
    if (it != this->channels_.end())
    {
        return it->second;
    }

    // Expected format: twitchSlug+kickSlug or twitch:slug+kick:slug
    QString twitchPart;
    QString kickPart;
    if (cleanName.contains(u'+'))
    {
        auto parts = cleanName.split(u'+');
        twitchPart = parts.first().trimmed();
        kickPart = parts.last().trimmed();
    }
    else
    {
        // Single name for both platforms (e.g. shroud -> Twitch shroud + Kick shroud)
        twitchPart = cleanName;
        kickPart = cleanName;
    }

    if (twitchPart.startsWith(QStringLiteral("twitch:"), Qt::CaseInsensitive))
    {
        twitchPart = twitchPart.mid(7).trimmed();
    }
    if (kickPart.startsWith(QStringLiteral("kick:"), Qt::CaseInsensitive))
    {
        kickPart = kickPart.mid(5).trimmed();
    }

    QString canonicalName;
    if (twitchPart.compare(kickPart, Qt::CaseInsensitive) == 0)
    {
        canonicalName = twitchPart;
    }
    else
    {
        canonicalName = twitchPart + u'+' + kickPart;
    }

    auto canIt = this->channels_.find(canonicalName.toLower());
    if (canIt != this->channels_.end())
    {
        return canIt->second;
    }

    auto twitchChan = getApp()->getTwitch()->getOrAddChannel(twitchPart);
    auto kickChan = getApp()->getKick()->getOrAddChannel(kickPart);

    auto combined = std::make_shared<CombinedChannel>(canonicalName, twitchChan, kickChan);
    this->channels_.emplace(canonicalName.toLower(), combined);
    if (cleanName.toLower() != canonicalName.toLower())
    {
        this->channels_.emplace(cleanName.toLower(), combined);
    }
    return combined;
}

ChannelPtr CombinedManager::getChannelOrEmpty(const QString &channelName)
{
    QString cleanName = channelName.trimmed();
    if (cleanName.startsWith(QStringLiteral("combined:"), Qt::CaseInsensitive))
    {
        cleanName = cleanName.mid(9).trimmed();
    }

    QString twitchPart;
    QString kickPart;
    if (cleanName.contains(u'+'))
    {
        auto parts = cleanName.split(u'+');
        twitchPart = parts.first().trimmed();
        kickPart = parts.last().trimmed();
    }
    else
    {
        twitchPart = cleanName;
        kickPart = cleanName;
    }

    if (twitchPart.startsWith(QStringLiteral("twitch:"), Qt::CaseInsensitive))
    {
        twitchPart = twitchPart.mid(7).trimmed();
    }
    if (kickPart.startsWith(QStringLiteral("kick:"), Qt::CaseInsensitive))
    {
        kickPart = kickPart.mid(5).trimmed();
    }

    QString canonicalName;
    if (twitchPart.compare(kickPart, Qt::CaseInsensitive) == 0)
    {
        canonicalName = twitchPart;
    }
    else
    {
        canonicalName = twitchPart + u'+' + kickPart;
    }

    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = this->channels_.find(canonicalName.toLower());
    if (it != this->channels_.end())
    {
        return it->second;
    }
    auto it2 = this->channels_.find(cleanName.toLower());
    if (it2 != this->channels_.end())
    {
        return it2->second;
    }

    return Channel::getEmpty();
}

}  // namespace chatterino
