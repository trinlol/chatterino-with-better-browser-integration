// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickEmotes.hpp"
#include "providers/kick/KickPusherClient.hpp"

#include <QObject>

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace chatterino {

class KickManager final : public QObject
{
    Q_OBJECT

public:
    explicit KickManager(QObject *parent = nullptr);
    ~KickManager() override;

    ChannelPtr getOrAddChannel(const QString &channelName);
    ChannelPtr getChannelOrEmpty(const QString &channelName);
    void forEachChannel(const std::function<void(ChannelPtr)> &forEach);

    KickPusherClient &pusherClient();
    KickEmotes &emotes();

    // Kick Account & Authentication
    bool hasAccount() const;
    QString getCurrentUsername() const;
    QString getAuthToken() const;
    QString getUserId() const;
    void setAccount(const QString &username, const QString &token,
                    const QString &userId = QString(),
                    const QString &refreshToken = QString());
    void removeAccount();

    void exchangeOAuthCode(
        const QString &code, const QString &codeVerifier,
        std::function<void(bool success, QString error, QString accessToken,
                           QString refreshToken)>
            callback);

    void fetchCurrentUser(
        const QString &token,
        std::function<void(bool success, QString error, QString username,
                           QString userId, QString avatarUrl)>
            callback);

    void verifyAccount(
        const QString &username, const QString &token,
        std::function<void(bool success, QString error, QString userId,
                           QString avatarUrl)>
            callback);

    // Kick Moderation API
    void banUser(const QString &channelSlug, const QString &username,
                 std::function<void(bool ok, QString error)> callback = nullptr);
    void timeoutUser(
        const QString &channelSlug, const QString &username,
        int durationSeconds,
        std::function<void(bool ok, QString error)> callback = nullptr);
    void unbanUser(const QString &channelSlug, const QString &username,
                   std::function<void(bool ok, QString error)> callback = nullptr);

Q_SIGNALS:
    void accountChanged();


private:
    KickPusherClient pusherClient_;
    KickEmotes emotes_;

    std::mutex mutex_;
    std::unordered_map<QString, std::shared_ptr<KickChannel>> channels_;
};

}  // namespace chatterino
