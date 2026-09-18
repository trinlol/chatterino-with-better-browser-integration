// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickManager.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace chatterino {


KickManager::KickManager(QObject *parent)
    : QObject(parent)
{
}

KickManager::~KickManager() = default;

ChannelPtr KickManager::getOrAddChannel(const QString &channelName)
{
    QString cleanName = channelName.trimmed();
    if (cleanName.startsWith(QStringLiteral("kick:"), Qt::CaseInsensitive))
    {
        cleanName = cleanName.mid(5).trimmed();
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

    auto channel = std::make_shared<KickChannel>(cleanName, this->pusherClient_, this->emotes_);
    this->channels_.emplace(cleanName.toLower(), channel);
    return channel;
}

ChannelPtr KickManager::getChannelOrEmpty(const QString &channelName)
{
    QString cleanName = channelName.trimmed();
    if (cleanName.startsWith(QStringLiteral("kick:"), Qt::CaseInsensitive))
    {
        cleanName = cleanName.mid(5).trimmed();
    }

    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = this->channels_.find(cleanName.toLower());
    if (it != this->channels_.end())
    {
        return it->second;
    }

    return Channel::getEmpty();
}

void KickManager::forEachChannel(const std::function<void(ChannelPtr)> &forEach)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    for (const auto &pair : this->channels_)
    {
        forEach(pair.second);
    }
}

KickPusherClient &KickManager::pusherClient()
{
    return this->pusherClient_;
}

KickEmotes &KickManager::emotes()
{
    return this->emotes_;
}

bool KickManager::hasAccount() const
{
    return !getSettings()->kickAccountToken.getValue().trimmed().isEmpty() &&
           !getSettings()->kickAccountUsername.getValue().trimmed().isEmpty();
}

QString KickManager::getCurrentUsername() const
{
    return getSettings()->kickAccountUsername.getValue();
}

QString KickManager::getAuthToken() const
{
    return getSettings()->kickAccountToken.getValue();
}

QString KickManager::getUserId() const
{
    return getSettings()->kickAccountUserId.getValue();
}

void KickManager::setAccount(const QString &username, const QString &token,
                            const QString &userId,
                            const QString &refreshToken)
{
    getSettings()->kickAccountUsername = username.trimmed();
    getSettings()->kickAccountToken = token.trimmed();
    getSettings()->kickAccountUserId = userId.trimmed();
    if (!refreshToken.trimmed().isEmpty())
    {
        getSettings()->kickAccountRefreshToken = refreshToken.trimmed();
    }
    Q_EMIT this->accountChanged();
}

void KickManager::removeAccount()
{
    getSettings()->kickAccountUsername = QString();
    getSettings()->kickAccountToken = QString();
    getSettings()->kickAccountUserId = QString();
    getSettings()->kickAccountRefreshToken = QString();
    Q_EMIT this->accountChanged();
}

void KickManager::exchangeOAuthCode(
    const QString &code, const QString &codeVerifier,
    std::function<void(bool success, QString error, QString accessToken,
                       QString refreshToken)>
        callback)
{
    auto clientId = getSettings()->kickClientId.getValue().trimmed();
    auto clientSecret = getSettings()->kickClientSecret.getValue().trimmed();
    auto redirectUri = QStringLiteral("http://localhost:52153/callback");

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"),
                       QStringLiteral("authorization_code"));
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("client_secret"), clientSecret);
    query.addQueryItem(QStringLiteral("code"), code.trimmed());
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("code_verifier"), codeVerifier.trimmed());

    QByteArray postData = query.toString(QUrl::FullyEncoded).toUtf8();

    NetworkRequest(QUrl(QStringLiteral("https://id.kick.com/oauth/token")),
                   NetworkRequestType::Post)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .header("Accept", "application/json")
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .payload(postData)
        .timeout(15000)
        .onSuccess([callback](NetworkResult result) {
            auto json = result.parseJson();
            QString accessToken =
                json.value(QStringLiteral("access_token")).toString();
            QString refreshToken =
                json.value(QStringLiteral("refresh_token")).toString();
            if (accessToken.isEmpty())
            {
                if (callback)
                {
                    callback(false,
                             QStringLiteral(
                                 "Missing access_token in Kick response."),
                             QString(), QString());
                }
                return;
            }
            if (callback)
            {
                callback(true, QString(), accessToken, refreshToken);
            }
        })
        .onError([callback](NetworkResult result) {
            QString errorMsg =
                QStringLiteral("Kick token exchange failed (HTTP %1): %2")
                    .arg(result.status().value_or(0))
                    .arg(QString::fromUtf8(result.getData()));
            if (callback)
            {
                callback(false, errorMsg, QString(), QString());
            }
        })
        .execute();
}

void KickManager::fetchCurrentUser(
    const QString &token,
    std::function<void(bool success, QString error, QString username,
                       QString userId, QString avatarUrl)>
        callback)
{
    QString cleanToken = token.trimmed();
    if (cleanToken.isEmpty())
    {
        if (callback)
        {
            callback(false, QStringLiteral("Token cannot be empty."), QString(),
                     QString(), QString());
        }
        return;
    }

    NetworkRequest(QUrl(QStringLiteral("https://api.kick.com/public/v1/users")))
        .header("Accept", "application/json")
        .header("Authorization", QStringLiteral("Bearer %1").arg(cleanToken))
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .timeout(12000)
        .onSuccess([callback](NetworkResult result) {
            auto root = result.parseJson();
            auto dataArray = root.value(QStringLiteral("data")).toArray();
            if (dataArray.isEmpty())
            {
                if (callback)
                {
                    callback(false,
                             QStringLiteral("No user data returned by Kick API."),
                             QString(), QString(), QString());
                }
                return;
            }

            auto userObj = dataArray.first().toObject();
            QString username = userObj.value(QStringLiteral("name")).toString();
            int64_t uid =
                userObj.value(QStringLiteral("user_id")).toVariant().toLongLong();
            QString avatar =
                userObj.value(QStringLiteral("profile_picture")).toString();

            if (callback)
            {
                callback(true, QString(), username, QString::number(uid),
                         avatar);
            }
        })
        .onError([callback](NetworkResult result) {
            QString errorMsg =
                QStringLiteral("Failed to fetch user profile (HTTP %1): %2")
                    .arg(result.status().value_or(0))
                    .arg(QString::fromUtf8(result.getData()));
            if (callback)
            {
                callback(false, errorMsg, QString(), QString(), QString());
            }
        })
        .execute();
}

void KickManager::verifyAccount(
    const QString &username, const QString &token,
    std::function<void(bool success, QString error, QString userId,
                       QString avatarUrl)>
        callback)
{
    QString cleanUser = username.trimmed();
    QString cleanToken = token.trimmed();
    if (cleanToken.isEmpty())
    {
        if (callback)
        {
            callback(false, QStringLiteral("Token cannot be empty."),
                     QString(), QString());
        }
        return;
    }

    // If username is empty, try fetching from official Kick users endpoint first
    if (cleanUser.isEmpty())
    {
        this->fetchCurrentUser(
            cleanToken,
            [callback](bool success, QString error, QString /*user*/,
                       QString userId, QString avatar) {
                if (callback)
                {
                    callback(success, error, userId, avatar);
                }
            });
        return;
    }

    auto url = QStringLiteral("https://kick.com/api/v2/channels/%1").arg(cleanUser);
    NetworkRequest(QUrl(url))
        .header("Accept", "application/json")
        .header("Authorization", QStringLiteral("Bearer %1").arg(cleanToken))
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .timeout(10000)
        .onSuccess([callback, cleanUser](NetworkResult result) {
            auto root = result.parseJson();
            auto userObj = root.value(QStringLiteral("user")).toObject();
            int64_t uid = userObj.value(QStringLiteral("id")).toVariant().toLongLong();
            QString avatar = userObj.value(QStringLiteral("profile_pic")).toString();
            QString actualUsername = userObj.value(QStringLiteral("username")).toString();
            if (actualUsername.isEmpty())
            {
                actualUsername = cleanUser;
            }

            if (callback)
            {
                callback(true, QString(), QString::number(uid), avatar);
            }
        })
        .onError([callback](NetworkResult result) {
            QString errorMsg = QStringLiteral("Verification failed (HTTP %1): %2")
                                   .arg(result.status().value_or(0))
                                   .arg(QString::fromUtf8(result.getData()));
            if (callback)
            {
                callback(false, errorMsg, QString(), QString());
            }
        })
        .execute();
}

void KickManager::banUser(const QString &channelSlug, const QString &username,
                          std::function<void(bool ok, QString error)> callback)
{
    if (!this->hasAccount())
    {
        if (callback)
        {
            callback(false, QStringLiteral("No Kick account configured."));
        }
        return;
    }

    auto url = QStringLiteral("https://kick.com/api/v2/channels/%1/bans").arg(channelSlug);
    QJsonObject json;
    json[QStringLiteral("banned_username")] = username.trimmed();
    json[QStringLiteral("permanent")] = true;

    NetworkRequest(QUrl(url), NetworkRequestType::Post)
        .header("Accept", "application/json")
        .header("Content-Type", "application/json")
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(this->getAuthToken()))
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .json(json)
        .timeout(10000)
        .onSuccess([callback](NetworkResult /*res*/) {
            if (callback)
            {
                callback(true, QString());
            }
        })
        .onError([callback](NetworkResult res) {
            if (callback)
            {
                callback(false, QStringLiteral("Ban failed (HTTP %1): %2")
                                    .arg(res.status().value_or(0))
                                    .arg(QString::fromUtf8(res.getData())));
            }
        })

        .execute();
}

void KickManager::timeoutUser(const QString &channelSlug, const QString &username,
                             int durationSeconds,
                             std::function<void(bool ok, QString error)> callback)
{
    if (!this->hasAccount())
    {
        if (callback)
        {
            callback(false, QStringLiteral("No Kick account configured."));
        }
        return;
    }

    auto url = QStringLiteral("https://kick.com/api/v2/channels/%1/bans").arg(channelSlug);
    QJsonObject json;
    json[QStringLiteral("banned_username")] = username.trimmed();
    json[QStringLiteral("permanent")] = false;
    // Kick API duration is typically in minutes (minimum 1 minute)
    int durationMinutes = std::max(1, durationSeconds / 60);
    json[QStringLiteral("duration")] = durationMinutes;

    NetworkRequest(QUrl(url), NetworkRequestType::Post)
        .header("Accept", "application/json")
        .header("Content-Type", "application/json")
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(this->getAuthToken()))
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .json(json)
        .timeout(10000)
        .onSuccess([callback](NetworkResult /*res*/) {
            if (callback)
            {
                callback(true, QString());
            }
        })
        .onError([callback](NetworkResult res) {
            if (callback)
            {
                callback(false, QStringLiteral("Timeout failed (HTTP %1): %2")
                                    .arg(res.status().value_or(0))
                                    .arg(QString::fromUtf8(res.getData())));
            }
        })
        .execute();
}

void KickManager::unbanUser(const QString &channelSlug, const QString &username,
                            std::function<void(bool ok, QString error)> callback)
{
    if (!this->hasAccount())
    {
        if (callback)
        {
            callback(false, QStringLiteral("No Kick account configured."));
        }
        return;
    }

    auto url = QStringLiteral("https://kick.com/api/v2/channels/%1/bans/%2")
                   .arg(channelSlug, username.trimmed());

    NetworkRequest(QUrl(url), NetworkRequestType::Delete)
        .header("Accept", "application/json")
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(this->getAuthToken()))
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .timeout(10000)
        .onSuccess([callback](NetworkResult /*res*/) {
            if (callback)
            {
                callback(true, QString());
            }
        })
        .onError([callback](NetworkResult res) {
            if (callback)
            {
                callback(false, QStringLiteral("Unban failed (HTTP %1): %2")
                                    .arg(res.status().value_or(0))
                                    .arg(QString::fromUtf8(res.getData())));
            }
        })

        .execute();
}

}  // namespace chatterino

