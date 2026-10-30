// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVectorModel.hpp"
#include "util/QCompareTransparent.hpp"

#include <pajlada/signals.hpp>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>

#include <map>
#include <memory>
#include <vector>

namespace chatterino {

class WatchStreakController;

class WatchStreakModel : public SignalVectorModel<QString>
{
public:
    explicit WatchStreakModel(QObject *parent);

protected:
    QString getItemFromRow(std::vector<QStandardItem *> &row,
                           const QString &original) override;
    void getRowFromItem(const QString &item,
                        std::vector<QStandardItem *> &row) override;

    friend class WatchStreakController;
};

class WatchStreakController final : public QObject
{
    Q_OBJECT

public:
    explicit WatchStreakController();
    ~WatchStreakController() override = default;

    void initialize();

    WatchStreakModel *createModel(QObject *parent);

    struct ActiveStreamSession {
        QString channelId;
        QString streamId;
        int minutesWatched = 0;
        QDateTime lastSent;
    };

    bool isChannelActive(const QString &channelName) const;
    int getMinutesWatched(const QString &channelName) const;

    pajlada::Signals::Signal<QString, int> minutesUpdated;

private:
    void onTick();
    void processChannelLive(const QString &channelName, const QString &channelId,
                            const QString &streamId, const QString &userId,
                            const QString &oauthToken);

    QTimer tickTimer_;

    // Map channel name (case-insensitive) -> active stream tracking session
    std::map<QString, ActiveStreamSession, QCompareCaseInsensitive> activeSessions_;
};

}  // namespace chatterino
