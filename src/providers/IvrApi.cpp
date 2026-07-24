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

#include <cstddef>

namespace chatterino {

namespace {

constexpr const char *USER_LOGS_BASE_URLS[] = {
    "https://logs.zonian.dev/",
    "https://logs.ivr.fi/",
};

using UserLogsJsonSuccessCallback = std::function<void(const NetworkResult &)>;

void requestUserLogsJsonImpl(QString path, const QUrlQuery &query,
                             int timeoutMs,
                             UserLogsJsonSuccessCallback successCallback,
                             IvrFailureCallback failureCallback,
                             std::size_t baseIndex)
{
    if (baseIndex >= std::size(USER_LOGS_BASE_URLS))
    {
        failureCallback();
        return;
    }

    QUrl url(QString(USER_LOGS_BASE_URLS[baseIndex]) + path);
    if (!query.isEmpty())
    {
        url.setQuery(query);
    }

    NetworkRequest(url)
        .timeout(timeoutMs)
        .header("Accept", "application/json")
        .onSuccess([successCallback](const NetworkResult &result) {
            successCallback(result);
        })
        .onError([path, query, timeoutMs, successCallback, failureCallback,
                  baseIndex](const NetworkResult &result) {
            const bool hasFallback =
                baseIndex + 1 < std::size(USER_LOGS_BASE_URLS);

            qCWarning(chatterinoIvr)
                << "Failed user logs API call at"
                << USER_LOGS_BASE_URLS[baseIndex] << result.formatError()
                << QString(result.getData())
                << (hasFallback ? ", trying fallback" : "");

            if (hasFallback)
            {
                requestUserLogsJsonImpl(path, query, timeoutMs, successCallback,
                                        failureCallback, baseIndex + 1);
            }
            else
            {
                failureCallback();
            }
        })
        .execute();
}

void requestUserLogsJson(QString path, QUrlQuery query, int timeoutMs,
                         UserLogsJsonSuccessCallback successCallback,
                         IvrFailureCallback failureCallback)
{
    requestUserLogsJsonImpl(std::move(path), std::move(query), timeoutMs,
                            std::move(successCallback),
                            std::move(failureCallback), 0);
}

}  // namespace

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

    const QString path = QString("channel/%1/user/%2/stats")
                             .arg(channelName.toLower(), userName.toLower());

    requestUserLogsJson(
        path, {}, 5 * 1000,
        [successCallback](const NetworkResult &result) {
            successCallback(result.parseJson());
        },
        failureCallback);
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

    requestUserLogsJson(
        path, query, 10 * 1000,
        [successCallback, channelName](const NetworkResult &result) {
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
        },
        failureCallback);
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
