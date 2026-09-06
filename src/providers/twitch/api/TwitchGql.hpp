// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser:
//  - Only the prediction-related GQL operations are ported.
//  - Auth uses the logged-in Chatterino account token; Moltorino's
//    separate saved-account system is not ported.
#pragma once

#include "providers/twitch/TwitchChannel.hpp"

#include <functional>
#include <optional>
#include <QString>
#include <QStringList>
#include <QVector>

namespace chatterino {

struct PredictionTemplate {
    QString title;
    QStringList outcomes;
    int durationSeconds = 120;
};

class TwitchGql
{
public:
    static void getActivePrediction(
        const QString &channelLogin, const QString &oauthToken,
        std::function<void(std::optional<TwitchChannel::PredictionEvent>)>
            successCallback,
        std::function<void(const QString &)> failureCallback);
    static void makePrediction(
        const QString &eventID, const QString &outcomeID, int points,
        const QString &oauthToken, std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void createPredictionEvent(
        const QString &channelId, const QString &title,
        const QStringList &outcomes, int predictionWindowSeconds,
        const QString &oauthToken, std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void getPredictionTemplates(
        const QString &channelLogin, const QString &oauthToken,
        std::function<void(QVector<PredictionTemplate>)> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void lockPrediction(
        const QString &eventId, const QString &oauthToken,
        std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void cancelPrediction(
        const QString &eventId, const QString &oauthToken,
        std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void resolvePrediction(
        const QString &eventId, const QString &outcomeId,
        const QString &oauthToken, std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void getChannelPoints(
        const QString &channelLogin, const QString &oauthToken,
        std::function<void(qint64)> successCallback,
        std::function<void(const QString &)> failureCallback);
};

}  // namespace chatterino
