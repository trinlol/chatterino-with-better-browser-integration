// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/twitch/WatchStreakController.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/NativeMessaging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/StandardItemHelper.hpp"

#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <ranges>

namespace {

void logWatchStreak(const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);

    const QString timeStr =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    const QString formatted = QString("[%1] %2").arg(timeStr, message);

    qCWarning(chatterinoTwitch).noquote() << formatted;

    if (auto *app = chatterino::tryGetApp())
    {
        const QString logPath =
            app->getPaths().miscDirectory + "/watchstreak.log";
        QFile file(logPath);
        if (file.open(QIODevice::Append | QIODevice::Text))
        {
            QTextStream out(&file);
            out << formatted << "\n";
        }
    }
}

}  // namespace

namespace chatterino {

WatchStreakModel::WatchStreakModel(QObject *parent)
    : SignalVectorModel<QString>(1, parent)
{
}

QString WatchStreakModel::getItemFromRow(std::vector<QStandardItem *> &row,
                                         const QString & /*original*/)
{
    return row[0]->data(Qt::DisplayRole).toString().trimmed();
}

void WatchStreakModel::getRowFromItem(const QString &item,
                                      std::vector<QStandardItem *> &row)
{
    setStringItem(row[0], item);
}

WatchStreakController::WatchStreakController()
{
    QObject::connect(&this->tickTimer_, &QTimer::timeout, this, [this] {
        this->onTick();
    });
}

void WatchStreakController::initialize()
{
    logWatchStreak(
        QString("[WatchStreak] Controller initialized. Automation enabled: %1")
            .arg(getSettings()->enableWatchStreakAutomation.getValue()
                     ? "true"
                     : "false"));

    getSettings()->enableWatchStreakAutomation.connect(
        [this](bool enabled) {
            logWatchStreak(
                QString("[WatchStreak] Automation setting changed to: %1")
                    .arg(enabled ? "ENABLED" : "DISABLED"));
            if (enabled)
            {
                if (!this->tickTimer_.isActive())
                {
                    this->tickTimer_.start(60 * 1000);
                }
                this->onTick();
            }
            else
            {
                this->tickTimer_.stop();
                this->activeSessions_.clear();
            }
        });

    if (getSettings()->enableWatchStreakAutomation.getValue())
    {
        this->tickTimer_.start(60 * 1000);
        this->onTick();
    }
}

WatchStreakModel *WatchStreakController::createModel(QObject *parent)
{
    auto *model = new WatchStreakModel(parent);
    model->initialize(&getSettings()->watchStreakChannels);
    return model;
}

bool WatchStreakController::isChannelActive(const QString &channelName) const
{
    auto it = this->activeSessions_.find(channelName);
    return it != this->activeSessions_.end() && it->second.minutesWatched > 0;
}

int WatchStreakController::getMinutesWatched(const QString &channelName) const
{
    auto it = this->activeSessions_.find(channelName);
    if (it != this->activeSessions_.end())
    {
        return it->second.minutesWatched;
    }
    return 0;
}

void WatchStreakController::onTick()
{
    if (!getSettings()->enableWatchStreakAutomation.getValue())
    {
        return;
    }

    auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (!account || account->isAnon())
    {
        return;
    }

    const QString userId = account->getUserId();
    const QString oauthToken = account->getOAuthToken();
    if (userId.isEmpty() || oauthToken.trimmed().isEmpty())
    {
        return;
    }

    auto channels = getSettings()->watchStreakChannels.readOnly();
    if (channels->empty())
    {
        this->activeSessions_.clear();
        return;
    }

    QStringList channelsNeedingHelix;
    std::unordered_set<QString> liveChannelsInTick;

    for (const auto &channelName : *channels)
    {
        if (channelName.trimmed().isEmpty())
        {
            continue;
        }

        auto chan = getApp()->getTwitch()->getChannelOrEmpty(channelName);
        auto twitchChannel = std::dynamic_pointer_cast<TwitchChannel>(chan);

        if (twitchChannel && twitchChannel->isLive())
        {
            auto status = twitchChannel->accessStreamStatus();
            QString streamId = status->streamId;
            QString channelId = twitchChannel->roomId();

            if (!streamId.isEmpty() && !channelId.isEmpty())
            {
                liveChannelsInTick.insert(channelName.toLower());
                this->processChannelLive(channelName, channelId, streamId, userId,
                                         oauthToken);
                continue;
            }
        }

        channelsNeedingHelix.append(channelName);
    }

    if (channelsNeedingHelix.isEmpty())
    {
        // Clean up offline sessions for channels not live
        for (auto it = this->activeSessions_.begin();
             it != this->activeSessions_.end();)
        {
            if (!liveChannelsInTick.contains(it->first.toLower()))
            {
                logWatchStreak(
                    QString("[WatchStreak] Channel %1 went offline (or stream ended). Total minutes watched: %2.")
                        .arg(it->first)
                        .arg(it->second.minutesWatched));
                it = this->activeSessions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return;
    }

    // Query Helix for channels not currently joined or whose stream status is not yet available
    getHelix()->fetchStreams(
        {}, channelsNeedingHelix,
        [this, userId, oauthToken, liveChannelsInTick](
            std::vector<HelixStream> streams) mutable {
            for (const auto &stream : streams)
            {
                if (stream.id.isEmpty() || stream.userId.isEmpty())
                {
                    continue;
                }

                liveChannelsInTick.insert(stream.userLogin.toLower());
                this->processChannelLive(stream.userLogin, stream.userId,
                                         stream.id, userId, oauthToken);
            }

            // Remove offline channels from active sessions
            for (auto it = this->activeSessions_.begin();
                 it != this->activeSessions_.end();)
            {
                if (!liveChannelsInTick.contains(it->first.toLower()))
                {
                    logWatchStreak(
                        QString("[WatchStreak] Channel %1 went offline (or stream ended). Total minutes watched: %2.")
                            .arg(it->first)
                            .arg(it->second.minutesWatched));
                    it = this->activeSessions_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        },
        [channelsNeedingHelix] {
            logWatchStreak(QString("[WatchStreak] Helix stream lookup failed for: %1")
                               .arg(channelsNeedingHelix.join(", ")));
        },
        [] {});
}

void WatchStreakController::processChannelLive(const QString &channelName,
                                               const QString &channelId,
                                               const QString &streamId,
                                               const QString &userId,
                                               const QString &oauthToken)
{
    auto it = this->activeSessions_.find(channelName);
    if (it == this->activeSessions_.end())
    {
        it = this->activeSessions_
                 .emplace(channelName,
                          ActiveStreamSession{
                              .channelId = channelId,
                              .streamId = streamId,
                              .minutesWatched = 0,
                              .lastSent = {},
                          })
                 .first;
        logWatchStreak(
            QString("[WatchStreak] Started tracking stream for %1 (streamId: %2, channelId: %3)")
                .arg(channelName, streamId, channelId));
    }
    else if (it->second.streamId != streamId)
    {
        logWatchStreak(
            QString("[WatchStreak] New broadcast detected for %1 (old streamId: %2, new streamId: %3, previous minutes: %4)")
                .arg(channelName, it->second.streamId, streamId)
                .arg(it->second.minutesWatched));
        it->second.channelId = channelId;
        it->second.streamId = streamId;
        it->second.minutesWatched = 0;
    }

    it->second.minutesWatched++;
    it->second.lastSent = QDateTime::currentDateTimeUtc();
    const int currentMinutes = it->second.minutesWatched;

    logWatchStreak(
        QString("[WatchStreak] Tracked minute #%1 for %2 (streamId: %3)")
            .arg(currentMinutes)
            .arg(channelName, streamId));

    this->minutesUpdated.invoke(channelName, currentMinutes);
}

}  // namespace chatterino
