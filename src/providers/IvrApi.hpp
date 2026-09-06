// SPDX-FileCopyrightText: 2020 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/network/NetworkRequest.hpp"
#include "providers/twitch/api/Helix.hpp"

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>

#include <functional>
#include <memory>
#include <vector>

namespace chatterino {

using IvrFailureCallback = std::function<void()>;
template <typename... T>
using ResultCallback = std::function<void(T...)>;

struct Message;
using MessagePtr = std::shared_ptr<const Message>;

struct IvrSubage {
    const bool isSubHidden;
    const bool isSubbed;
    const QString subTier;
    const int totalSubMonths;
    const QString followingSince;

    IvrSubage(const QJsonObject &root)
        : isSubHidden(root.value("statusHidden").toBool())
        , isSubbed(!root.value("meta").isNull())
        , subTier(root.value("meta").toObject().value("tier").toString())
        , totalSubMonths(
              root.value("cumulative").toObject().value("months").toInt())
        , followingSince(root.value("followedAt").toString())
    {
    }
};

struct IvrUserBadge {
    QString setID;
    QString version;
    QString title;
    QString description;

    IvrUserBadge(const QJsonObject &root)
        : setID(root.value("setID").toString())
        , version(root.value("version").toString())
        , title(root.value("title").toString())
        , description(root.value("description").toString())
    {
    }
};

struct IvrChannelUserStats {
    QString userId;
    QString userLogin;
    int messageCount = 0;

    IvrChannelUserStats(const QJsonObject &root)
        : userId(root.value("userId").toString())
        , userLogin(root.value("userLogin").toString())
        , messageCount(root.value("messageCount").toInt())
    {
    }
};

class IvrApi final
{
public:
    // https://api.ivr.fi/v2/docs/static/index.html#/Twitch/get_twitch_subage__user___channel_
    void getSubage(QString userName, QString channelName,
                   ResultCallback<IvrSubage> resultCallback,
                   IvrFailureCallback failureCallback);

    // https://api.ivr.fi/v2/twitch/user?login=<user>
    void getUserBadges(QString userName,
                       ResultCallback<std::vector<IvrUserBadge>> resultCallback,
                       IvrFailureCallback failureCallback);

    // https://logs.ivr.fi/docs (mirrored at https://logs.zonian.dev)
    void getChannelUserStats(QString channelName, QString userName,
                             ResultCallback<IvrChannelUserStats> resultCallback,
                             IvrFailureCallback failureCallback);

    // https://logs.zonian.dev/channel/{channel}/user/{user}?json=true
    void loadUserLogsForDay(
        QString channelName, QString userName, QDate day, int limit,
        ResultCallback<std::vector<MessagePtr>> successCallback,
        IvrFailureCallback failureCallback);

    // https://api.ivr.fi/v2/docs/static/index.html#/Twitch/post_twitch_founders__channel_
    // Ported from Moltorino (MIT, (c) MoltoBenne)
    void getFounders(QString channelName,
                     ResultCallback<std::vector<HelixModerator>> resultCallback,
                     IvrFailureCallback failureCallback);

    static void initialize();

    IvrApi() = default;

    IvrApi(const IvrApi &) = delete;
    IvrApi &operator=(const IvrApi &) = delete;

    IvrApi(IvrApi &&) = delete;
    IvrApi &operator=(IvrApi &&) = delete;

private:
    NetworkRequest makeRequest(QString url, QUrlQuery urlQuery);
};

IvrApi *getIvr();

}  // namespace chatterino
