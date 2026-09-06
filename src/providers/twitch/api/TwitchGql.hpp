// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser:
//  - Prediction GQL operations plus the blocked-term, user-lookup and
//    channel-role (mod/vip/lead-mod/editor) mutations used by the
//    /blockterm, /unblockterm, /leadmod and /unleadmod commands.
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

struct GqlBlockedTerm {
    QString id;
    QString phrase;
    QString expiresAt;
    bool isModEditable = false;
    int hitCount = 0;
};

struct GqlUser {
    QString id;
    QString login;
    QString displayName;
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

    // Blocked terms (/blockterm, /unblockterm)
    static void addChannelBlockedTerm(
        const QString &channelId, const QString &phrase,
        const QString &oauthToken, std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void getChannelBlockedTerms(
        const QString &channelId, const QString &oauthToken,
        std::function<void(QVector<GqlBlockedTerm>)> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void deleteChannelBlockedTerm(
        const QString &channelId, const QString &termId,
        const QString &oauthToken, std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);

    // User lookup (needed to resolve /leadmod targets to user ids)
    static void getUserByLogin(
        const QString &login, const QString &oauthToken,
        std::function<void(std::optional<GqlUser>)> successCallback,
        std::function<void(const QString &)> failureCallback);

    // Channel roles (/leadmod, /unleadmod)
    static void assignLeadModerator(
        const QString &channelId, const QString &targetUserId,
        const QString &oauthToken, std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
    static void unassignLeadModerator(
        const QString &channelId, const QString &targetUserId,
        const QString &oauthToken, std::function<void()> successCallback,
        std::function<void(const QString &)> failureCallback);
};

}  // namespace chatterino
