// SPDX-FileCopyrightText: 2020 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/IvrApi.hpp"

#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Message.hpp"
#include "providers/recentmessages/Impl.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/PostToThread.hpp"

#include <QDateTime>
#include <QUrlQuery>

namespace chatterino {

static IvrApi *instance = nullptr;

void IvrApi::getSubage(QString userName, QString channelName,
                       ResultCallback<IvrSubage> successCallback,
                       IvrFailureCallback failureCallback)
{
    assert(!userName.isEmpty() && !channelName.isEmpty());

    this->makeRequest(
            QString("twitch/subage/%1/%2").arg(userName).arg(channelName), {})
        .onSuccess([successCallback, failureCallback](auto result) {
            auto root = result.parseJson();

            successCallback(root);
        })
        .onError([failureCallback](auto result) {
            qCWarning(chatterinoIvr)
                << "Failed IVR API Call!" << result.formatError()
                << QString(result.getData());
            failureCallback();
        })
        .execute();
}

void IvrApi::getUserBadges(
    QString userName, ResultCallback<std::vector<IvrUserBadge>> successCallback,
    IvrFailureCallback failureCallback)
{
    assert(!userName.isEmpty());

    QUrlQuery query;
    query.addQueryItem("login", userName);

    this->makeRequest("twitch/user", query)
        .onSuccess([successCallback](auto result) {
            const auto root = result.parseJsonArray();
            if (root.isEmpty())
            {
                successCallback({});
                return;
            }

            std::vector<IvrUserBadge> badges;
            const auto badgeArray =
                root.at(0).toObject().value("badges").toArray();
            badges.reserve(badgeArray.size());
            for (const auto &badgeValue : badgeArray)
            {
                badges.emplace_back(badgeValue.toObject());
            }

            successCallback(std::move(badges));
        })
        .onError([failureCallback](auto result) {
            qCWarning(chatterinoIvr)
                << "Failed IVR user badges API call!" << result.formatError()
                << QString(result.getData());
            failureCallback();
        })
        .execute();
}

void IvrApi::getChannelUserStats(
    QString channelName, QString userName,
    ResultCallback<IvrChannelUserStats> successCallback,
    IvrFailureCallback failureCallback)
{
    assert(!channelName.isEmpty() && !userName.isEmpty());

    const QString url = QString("channel/%1/user/%2/stats")
                            .arg(channelName.toLower(), userName.toLower());

    NetworkRequest(QUrl("https://logs.zonian.dev/" + url))
        .timeout(5 * 1000)
        .header("Accept", "application/json")
        .onSuccess([successCallback](auto result) {
            successCallback(result.parseJson());
        })
        .onError([channelName, userName, successCallback,
                  failureCallback](auto result) {
            qCWarning(chatterinoIvr)
                << "Failed zonian logs API call, trying ivr.fi fallback"
                << result.formatError() << QString(result.getData());

            const QString fallbackUrl =
                QString("https://logs.ivr.fi/channel/%1/user/%2/stats")
                    .arg(channelName.toLower(), userName.toLower());

            NetworkRequest(QUrl(fallbackUrl))
                .timeout(5 * 1000)
                .header("Accept", "application/json")
                .onSuccess([successCallback](auto fallbackResult) {
                    successCallback(fallbackResult.parseJson());
                })
                .onError([failureCallback](auto fallbackResult) {
                    qCWarning(chatterinoIvr)
                        << "Failed logs API call!" << fallbackResult.formatError()
                        << QString(fallbackResult.getData());
                    failureCallback();
                })
                .execute();
        })
        .execute();
}

void IvrApi::loadUserLogsForDay(
    QString channelName, QString userName, QDate day, int limit,
    ResultCallback<std::vector<MessagePtr>> successCallback,
    IvrFailureCallback failureCallback)
{
    assert(!channelName.isEmpty() && !userName.isEmpty() && day.isValid());

    QUrlQuery query;
    query.addQueryItem("json", "true");
    query.addQueryItem("limit", QString::number(limit));
    query.addQueryItem("reverse", "false");

    const QDateTime from(day, QTime(0, 0), Qt::UTC);
    const QDateTime to = from.addDays(1);
    query.addQueryItem("from", from.toString(Qt::ISODateWithMs));
    query.addQueryItem("to", to.toString(Qt::ISODateWithMs));

    const QString path = QString("channel/%1/user/%2")
                             .arg(channelName.toLower(), userName.toLower());
    QUrl url("https://logs.zonian.dev/" + path);
    url.setQuery(query);

    NetworkRequest(url)
        .timeout(10 * 1000)
        .header("Accept", "application/json")
        .onSuccess([successCallback, channelName](const NetworkResult &result) {
            auto root = result.parseJson();
            auto parsed = recentmessages::detail::parseUserLogMessages(root);
            if (parsed.empty())
            {
                runInGuiThread([successCallback]() {
                    successCallback({});
                });
                return;
            }

            std::reverse(parsed.begin(), parsed.end());

            runInGuiThread([successCallback, channelName,
                            parsed = std::move(parsed)]() mutable {
                auto channel = std::make_shared<TwitchChannel>(channelName);
                auto built = recentmessages::detail::buildRecentMessages(
                    parsed, channel.get());
                successCallback(std::move(built));
            });
        })
        .onError([failureCallback](const NetworkResult &result) {
            qCWarning(chatterinoIvr)
                << "Failed to load user logs!" << result.formatError()
                << QString(result.getData());
            failureCallback();
        })
        .execute();
}

NetworkRequest IvrApi::makeRequest(QString url, QUrlQuery urlQuery)
{
    assert(!url.startsWith("/"));

    const QString baseUrl("https://api.ivr.fi/v2/");
    QUrl fullUrl(baseUrl + url);
    fullUrl.setQuery(urlQuery);

    return NetworkRequest(fullUrl).timeout(5 * 1000).header("Accept",
                                                            "application/json");
}

void IvrApi::initialize()
{
    assert(instance == nullptr);

    instance = new IvrApi();
}

IvrApi *getIvr()
{
    assert(instance != nullptr);

    return instance;
}

}  // namespace chatterino
