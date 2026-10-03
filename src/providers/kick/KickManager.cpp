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

void KickManager::refreshOAuthToken(
    std::function<void(bool success, QString error)> callback)
{
    auto refreshToken =
        getSettings()->kickAccountRefreshToken.getValue().trimmed();
    if (refreshToken.isEmpty())
    {
        if (callback)
        {
            callback(false, QStringLiteral("No Kick refresh token available."));
        }
        return;
    }

    auto clientId = getSettings()->kickClientId.getValue().trimmed();
    auto clientSecret = getSettings()->kickClientSecret.getValue().trimmed();

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"),
                       QStringLiteral("refresh_token"));
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    if (!clientSecret.isEmpty())
    {
        query.addQueryItem(QStringLiteral("client_secret"), clientSecret);
    }
    query.addQueryItem(QStringLiteral("refresh_token"), refreshToken);

    QByteArray postData = query.toString(QUrl::FullyEncoded).toUtf8();

    NetworkRequest(QUrl(QStringLiteral("https://id.kick.com/oauth/token")),
                   NetworkRequestType::Post)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .header("Accept", "application/json")
        .header("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
        .payload(postData)
        .timeout(15000)
        .onSuccess([this, callback](NetworkResult result) {
            auto json = result.parseJson();
            QString accessToken =
                json.value(QStringLiteral("access_token")).toString();
            QString newRefreshToken =
                json.value(QStringLiteral("refresh_token")).toString();
            if (accessToken.isEmpty())
            {
                if (callback)
                {
                    callback(
                        false,
                        QStringLiteral("Missing access_token in Kick refresh response."));
                }
                return;
            }

            getSettings()->kickAccountToken = accessToken.trimmed();
            if (!newRefreshToken.trimmed().isEmpty())
            {
                getSettings()->kickAccountRefreshToken =
                    newRefreshToken.trimmed();
            }
            Q_EMIT this->accountChanged();

            if (callback)
            {
                callback(true, QString());
            }
        })
        .onError([callback](NetworkResult result) {
            QString errorMsg =
                QStringLiteral("Kick token refresh failed (HTTP %1): %2")
                    .arg(result.status().value_or(0))
                    .arg(QString::fromUtf8(result.getData()));
            if (callback)
            {
                callback(false, errorMsg);
            }
        })
        .execute();
}

void KickManager::resolveUserId(
    const QString &usernameOrSlug,
    std::function<void(int64_t userId, QString error)> callback)
{
    QString target = usernameOrSlug.trimmed();
    if (target.startsWith(u'@'))
    {
        target = target.mid(1).trimmed();
    }
    if (target.isEmpty())
    {
        if (callback)
        {
            callback(0, QStringLiteral("Username cannot be empty."));
        }
        return;
    }

    bool isNum = false;
    int64_t parsed = target.toLongLong(&isNum);
    if (isNum && parsed > 0)
    {
        if (callback)
        {
            callback(parsed, QString());
        }
        return;
    }

    auto fetchV2 = [callback, target]() {
        auto v2Url = QStringLiteral("https://kick.com/api/v2/channels/%1")
                         .arg(target.toLower());
        NetworkRequest(QUrl(v2Url))
            .header("Accept", "application/json")
            .header("User-Agent",
                    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
            .timeout(10000)
            .onSuccess([callback, target](NetworkResult v2Res) {
                auto v2Root = v2Res.parseJson();
                int64_t uid = v2Root.value(QStringLiteral("user_id"))
                                  .toVariant()
                                  .toLongLong();
                if (uid <= 0)
                {
                    uid = v2Root.value(QStringLiteral("user"))
                              .toObject()
                              .value(QStringLiteral("id"))
                              .toVariant()
                              .toLongLong();
                }
                if (uid > 0)
                {
                    if (callback)
                    {
                        callback(uid, QString());
                    }
                }
                else
                {
                    if (callback)
                    {
                        callback(0, QStringLiteral("User '%1' not found on Kick.").arg(target));
                    }
                }
            })
            .onError([callback, target](NetworkResult res) {
                if (callback)
                {
                    callback(0, QStringLiteral("Could not resolve user '%1' on Kick (HTTP %2).")
                                    .arg(target)
                                    .arg(res.status().value_or(0)));
                }
            })
            .execute();
    };

    if (this->hasAccount())
    {
        auto url = QStringLiteral("https://api.kick.com/public/v1/channels?slug=%1")
                       .arg(target.toLower());
        NetworkRequest(QUrl(url))
            .header("Accept", "application/json")
            .header("Authorization",
                    QStringLiteral("Bearer %1").arg(this->getAuthToken()))
            .header("User-Agent",
                    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36")
            .timeout(10000)
            .onSuccess([callback, fetchV2](NetworkResult result) {
                auto root = result.parseJson();
                auto dataArr = root.value(QStringLiteral("data")).toArray();
                if (!dataArr.isEmpty())
                {
                    int64_t uid = dataArr.first()
                                      .toObject()
                                      .value(QStringLiteral("broadcaster_user_id"))
                                      .toVariant()
                                      .toLongLong();
                    if (uid > 0)
                    {
                        if (callback)
                        {
                            callback(uid, QString());
                        }
                        return;
                    }
                }
                fetchV2();
            })
            .onError([fetchV2](NetworkResult /*res*/) {
                fetchV2();
            })
            .execute();
    }
    else
    {
        fetchV2();
    }
}

void KickManager::banUser(const QString &channelSlug, const QString &username,
                          int64_t broadcasterUserId,
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

    auto executeBan = [this, callback](int64_t bId, int64_t uId) {
        QJsonObject json;
        json[QStringLiteral("broadcaster_user_id")] = static_cast<qint64>(bId);
        json[QStringLiteral("user_id")] = static_cast<qint64>(uId);

        NetworkRequest(
            QUrl(QStringLiteral("https://api.kick.com/public/v1/moderation/bans")),
            NetworkRequestType::Post)
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
    };

    auto resolveTarget = [this, callback, executeBan](int64_t bId, const QString &uName) {
        this->resolveUserId(uName, [callback, executeBan, bId, uName](int64_t uId, QString err) {
            if (uId <= 0)
            {
                if (callback)
                {
                    callback(false, err.isEmpty()
                                        ? QStringLiteral("Could not find user '%1' on Kick.").arg(uName)
                                        : err);
                }
                return;
            }
            executeBan(bId, uId);
        });
    };

    if (broadcasterUserId > 0)
    {
        resolveTarget(broadcasterUserId, username);
    }
    else
    {
        this->resolveUserId(channelSlug, [callback, resolveTarget, username](int64_t bId, QString err) {
            if (bId <= 0)
            {
                if (callback)
                {
                    callback(false, err.isEmpty()
                                        ? QStringLiteral("Could not find channel broadcaster ID.")
                                        : err);
                }
                return;
            }
            resolveTarget(bId, username);
        });
    }
}

void KickManager::timeoutUser(const QString &channelSlug, const QString &username,
                              int durationSeconds,
                              int64_t broadcasterUserId,
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

    int durationMinutes = std::clamp(durationSeconds / 60, 1, 10080);

    auto executeTimeout = [this, callback, durationMinutes](int64_t bId, int64_t uId) {
        QJsonObject json;
        json[QStringLiteral("broadcaster_user_id")] = static_cast<qint64>(bId);
        json[QStringLiteral("user_id")] = static_cast<qint64>(uId);
        json[QStringLiteral("duration")] = durationMinutes;

        NetworkRequest(
            QUrl(QStringLiteral("https://api.kick.com/public/v1/moderation/bans")),
            NetworkRequestType::Post)
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
    };

    auto resolveTarget = [this, callback, executeTimeout](int64_t bId, const QString &uName) {
        this->resolveUserId(uName, [callback, executeTimeout, bId, uName](int64_t uId, QString err) {
            if (uId <= 0)
            {
                if (callback)
                {
                    callback(false, err.isEmpty()
                                        ? QStringLiteral("Could not find user '%1' on Kick.").arg(uName)
                                        : err);
                }
                return;
            }
            executeTimeout(bId, uId);
        });
    };

    if (broadcasterUserId > 0)
    {
        resolveTarget(broadcasterUserId, username);
    }
    else
    {
        this->resolveUserId(channelSlug, [callback, resolveTarget, username](int64_t bId, QString err) {
            if (bId <= 0)
            {
                if (callback)
                {
                    callback(false, err.isEmpty()
                                        ? QStringLiteral("Could not find channel broadcaster ID.")
                                        : err);
                }
                return;
            }
            resolveTarget(bId, username);
        });
    }
}

void KickManager::unbanUser(const QString &channelSlug, const QString &username,
                            int64_t broadcasterUserId,
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

    auto executeUnban = [this, callback](int64_t bId, int64_t uId) {
        QJsonObject json;
        json[QStringLiteral("broadcaster_user_id")] = static_cast<qint64>(bId);
        json[QStringLiteral("user_id")] = static_cast<qint64>(uId);

        NetworkRequest(
            QUrl(QStringLiteral("https://api.kick.com/public/v1/moderation/bans")),
            NetworkRequestType::Delete)
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
                    callback(false, QStringLiteral("Unban failed (HTTP %1): %2")
                                        .arg(res.status().value_or(0))
                                        .arg(QString::fromUtf8(res.getData())));
                }
            })
            .execute();
    };

    auto resolveTarget = [this, callback, executeUnban](int64_t bId, const QString &uName) {
        this->resolveUserId(uName, [callback, executeUnban, bId, uName](int64_t uId, QString err) {
            if (uId <= 0)
            {
                if (callback)
                {
                    callback(false, err.isEmpty()
                                        ? QStringLiteral("Could not find user '%1' on Kick.").arg(uName)
                                        : err);
                }
                return;
            }
            executeUnban(bId, uId);
        });
    };

    if (broadcasterUserId > 0)
    {
        resolveTarget(broadcasterUserId, username);
    }
    else
    {
        this->resolveUserId(channelSlug, [callback, resolveTarget, username](int64_t bId, QString err) {
            if (bId <= 0)
            {
                if (callback)
                {
                    callback(false, err.isEmpty()
                                        ? QStringLiteral("Could not find channel broadcaster ID.")
                                        : err);
                }
                return;
            }
            resolveTarget(bId, username);
        });
    }
}

}  // namespace chatterino

