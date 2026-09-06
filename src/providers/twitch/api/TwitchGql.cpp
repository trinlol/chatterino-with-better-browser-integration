// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser:
//  - Prediction, blocked-term, role, user-lookup and channel-point reward
//    operations are ported; other Moltorino operations (polls, watch
//    track, follow) are not.
//  - Auth uses the logged-in Chatterino account token; Moltorino's
//    separate saved-account system is not ported.
#include "providers/twitch/api/TwitchGql.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "util/Helpers.hpp"
#include "util/RapidjsonHelpers.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>

namespace chatterino {

namespace {

    constexpr auto TWITCH_GQL_BROWSER_CLIENT_VERSION =
        "ef928475-9403-42f2-8a34-55784bd08e16";
    constexpr auto TWITCH_GQL_BROWSER_USER_AGENT =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/108.0.0.0 Safari/537.36";
    constexpr auto TWITCH_GQL_TV_CLIENT_ID = "ue6666qo983tsx6so1t0vnawi233wa";
    constexpr auto TWITCH_GQL_TV_USER_AGENT =
        "Mozilla/5.0 (Linux; Android 7.1; Smart Box C1) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/108.0.0.0 Safari/537.36";
    constexpr auto TWITCH_GQL_TV_ORIGIN = "https://android.tv.twitch.tv";
    constexpr auto TWITCH_GQL_TV_REFERER = "https://android.tv.twitch.tv/";
    constexpr int TWITCH_GQL_TIMEOUT_MS = 15 * 1000;

    const QString &twitchGqlDeviceId()
    {
        static const QString deviceId = [] {
            auto uuid = generateUuid();
            uuid.remove('{').remove('}').remove('-');
            return uuid;
        }();
        return deviceId;
    }

    const QString &twitchGqlSessionId()
    {
        static const QString sessionId = [] {
            auto uuid = generateUuid();
            uuid.remove('{').remove('}').remove('-');
            return uuid;
        }();
        return sessionId;
    }

    NetworkRequest makeInlineGqlRequest(const char *query,
                                        const QJsonObject &variables,
                                        const QString &oauthToken)
    {
        QJsonObject payload;
        payload.insert("query", query);
        payload.insert("variables", variables);

        QJsonArray payloadArray;
        payloadArray.append(payload);

        auto request =
            NetworkRequest("https://gql.twitch.tv/gql", NetworkRequestType::Post)
                .timeout(TWITCH_GQL_TIMEOUT_MS)
                .header("Client-Id", "kimne78kx3ncx6brgo4mv6wki5h1ko")
                .header("Client-Session-Id", twitchGqlSessionId())
                .header("Client-Version", TWITCH_GQL_BROWSER_CLIENT_VERSION)
                .header("User-Agent", TWITCH_GQL_BROWSER_USER_AGENT)
                .header("X-Device-Id", twitchGqlDeviceId())
                .json(payloadArray);

        if (!oauthToken.trimmed().isEmpty())
        {
            request = std::move(request).header("Authorization",
                                                "OAuth " + oauthToken);
        }

        return request;
    }

    NetworkRequest makePersistedGqlRequest(const QString &operationName,
                                           const QString &sha256Hash,
                                           const QJsonObject &variables,
                                           const QString &oauthToken)
    {
        QJsonObject payload;
        payload.insert("operationName", operationName);
        payload.insert("variables", variables);

        QJsonObject persistedQuery;
        persistedQuery.insert("version", 1);
        persistedQuery.insert("sha256Hash", sha256Hash);

        QJsonObject extensions;
        extensions.insert("persistedQuery", persistedQuery);
        payload.insert("extensions", extensions);

        QJsonArray payloadArray;
        payloadArray.append(payload);

        auto request =
            NetworkRequest("https://gql.twitch.tv/gql", NetworkRequestType::Post)
                .timeout(TWITCH_GQL_TIMEOUT_MS)
                .header("Client-Id", "kimne78kx3ncx6brgo4mv6wki5h1ko")
                .header("Client-Session-Id", twitchGqlSessionId())
                .header("Client-Version", TWITCH_GQL_BROWSER_CLIENT_VERSION)
                .header("User-Agent", TWITCH_GQL_BROWSER_USER_AGENT)
                .header("X-Device-Id", twitchGqlDeviceId())
                .json(payloadArray);

        if (!oauthToken.trimmed().isEmpty())
        {
            request = std::move(request).header("Authorization",
                                                "OAuth " + oauthToken);
        }

        return request;
    }

    /// Returns the GQL payload object - either the first element of a batch
    /// response array or the object response itself.
    const rapidjson::Value *gqlPayload(const rapidjson::Document &doc)
    {
        if (doc.IsArray() && doc.Size() > 0 && doc[0].IsObject())
        {
            return &doc[0];
        }
        if (doc.IsObject())
        {
            return &doc;
        }
        return nullptr;
    }

    NetworkRequest makeTvPersistedGqlRequest(const QString &operationName,
                                             const QString &sha256Hash,
                                             const QJsonObject &variables,
                                             const QString &oauthToken)
    {
        QJsonObject payload;
        payload.insert("operationName", operationName);
        payload.insert("variables", variables);

        QJsonObject persistedQuery;
        persistedQuery.insert("version", 1);
        persistedQuery.insert("sha256Hash", sha256Hash);

        QJsonObject extensions;
        extensions.insert("persistedQuery", persistedQuery);
        payload.insert("extensions", extensions);

        QJsonArray payloadArray;
        payloadArray.append(payload);

        auto request =
            NetworkRequest("https://gql.twitch.tv/gql", NetworkRequestType::Post)
                .timeout(TWITCH_GQL_TIMEOUT_MS)
                .header("Client-Id", TWITCH_GQL_TV_CLIENT_ID)
                .header("Client-Session-Id", twitchGqlSessionId())
                .header("Client-Version", TWITCH_GQL_BROWSER_CLIENT_VERSION)
                .header("Origin", TWITCH_GQL_TV_ORIGIN)
                .header("Referer", TWITCH_GQL_TV_REFERER)
                .header("User-Agent", TWITCH_GQL_TV_USER_AGENT)
                .header("X-Device-Id", twitchGqlDeviceId())
                .json(payloadArray);

        if (!oauthToken.trimmed().isEmpty())
        {
            request = std::move(request).header("Authorization",
                                                "OAuth " + oauthToken);
        }

        return request;
    }

    /// Returns the "data" object of a batched GQL response, matching the
    /// payload whose extension operationName equals operationName.
    const rapidjson::Value *gqlDataForOperation(const rapidjson::Document &doc,
                                                const QString &operationName)
    {
        if (doc.IsArray())
        {
            for (const auto &payloadValue : doc.GetArray())
            {
                if (!payloadValue.IsObject())
                {
                    continue;
                }
                const auto &payload = payloadValue.GetObject();

                QString payloadOperation;
                if (payload.HasMember("extensions") &&
                    payload["extensions"].IsObject() &&
                    rj::getSafe(payload["extensions"], "operationName",
                                payloadOperation) &&
                    payloadOperation.compare(operationName,
                                             Qt::CaseInsensitive) == 0 &&
                    payload.HasMember("data") &&
                    payload["data"].IsObject())
                {
                    return &payload["data"];
                }
            }
            return nullptr;
        }

        if (doc.IsObject() && doc.HasMember("data") && doc["data"].IsObject())
        {
            return &doc["data"];
        }
        return nullptr;
    }

    /// Returns the "data" object of a GQL response, or null if absent.
    const rapidjson::Value *gqlData(const rapidjson::Document &doc)
    {
        const auto *payload = gqlPayload(doc);
        if (payload == nullptr || !payload->HasMember("data") ||
            !(*payload)["data"].IsObject())
        {
            return nullptr;
        }
        return &(*payload)["data"];
    }

    /// Returns the first error message of a GQL response, or an empty string.
    QString gqlFirstErrorMessage(const rapidjson::Document &doc)
    {
        const auto *payload = gqlPayload(doc);
        if (payload == nullptr || !payload->HasMember("errors") ||
            !(*payload)["errors"].IsArray() || (*payload)["errors"].Empty())
        {
            return {};
        }

        for (const auto &error : (*payload)["errors"].GetArray())
        {
            QString message;
            if (rj::getSafe(error, "message", message) && !message.isEmpty())
            {
                return message;
            }
        }

        return "Twitch rejected the request";
    }

    /// Checks a TV-client mutation response: the payload object under
    /// mutationKey must exist and its "error" value must be absent/null.
    /// When successFlagName is non-empty, that flag must also be true.
    bool checkTvMutationResponse(const rapidjson::Document &doc,
                                 const QString &operationName,
                                 const char *mutationKey,
                                 const char *successFlagName,
                                 const QString &fallbackError,
                                 QString &errorMessage)
    {
        errorMessage = gqlFirstErrorMessage(doc);
        if (!errorMessage.isEmpty())
        {
            return false;
        }

        const auto *data = gqlDataForOperation(doc, operationName);
        if (data == nullptr || !data->HasMember(mutationKey) ||
            !(*data)[mutationKey].IsObject())
        {
            errorMessage = fallbackError;
            return false;
        }

        const auto &payload = (*data)[mutationKey];
        if (payload.HasMember("error"))
        {
            const auto &payloadError = payload["error"];
            if (!payloadError.IsNull())
            {
                if (payloadError.IsString())
                {
                    errorMessage = QString::fromUtf8(
                        payloadError.GetString());
                }
                else if (payloadError.IsObject())
                {
                    if (payloadError.HasMember("code") &&
                        payloadError["code"].IsString())
                    {
                        errorMessage = QString::fromUtf8(
                            payloadError["code"].GetString());
                    }
                    else if (payloadError.HasMember("message") &&
                             payloadError["message"].IsString())
                    {
                        errorMessage = QString::fromUtf8(
                            payloadError["message"].GetString());
                    }
                }
                errorMessage = fallbackError + ": " + errorMessage;
                return false;
            }
        }

        if (successFlagName != nullptr && strlen(successFlagName) > 0 &&
            (!payload.HasMember(successFlagName) ||
             !payload[successFlagName].IsBool() ||
             !payload[successFlagName].GetBool()))
        {
            errorMessage = fallbackError;
            return false;
        }

        return true;
    }

    /// Checks a mutation response where success means the mutation key exists
    /// in data with either no error or a null error. Sets errorMessage when
    /// the mutation failed.
    bool checkMutationResponse(const rapidjson::Document &doc,
                               const char *mutationKey,
                               bool requirePayloadObject, QString &errorMessage)
    {
        errorMessage = gqlFirstErrorMessage(doc);
        if (!errorMessage.isEmpty())
        {
            return false;
        }

        const auto *data = gqlData(doc);
        if (data == nullptr || !data->HasMember(mutationKey))
        {
            errorMessage = QStringLiteral("missing data");
            return false;
        }

        const auto &payload = (*data)[mutationKey];
        if (requirePayloadObject)
        {
            if (!payload.IsObject())
            {
                errorMessage = QStringLiteral("invalid response");
                return false;
            }
            if (payload.HasMember("error") && payload["error"].IsObject() &&
                !payload["error"].IsNull())
            {
                const auto &payloadError = payload["error"];
                if (payloadError.HasMember("code") &&
                    payloadError["code"].IsString())
                {
                    errorMessage = QString::fromUtf8(
                        payloadError["code"].GetString());
                }
                else if (payloadError.HasMember("message") &&
                         payloadError["message"].IsString())
                {
                    errorMessage = QString::fromUtf8(
                        payloadError["message"].GetString());
                }
                return false;
            }
        }

        return true;
    }

    QString predictionCreateOutcomeColor(int index, int outcomeCount)
    {
        if (outcomeCount == 2)
        {
            return index == 0 ? "BLUE" : "PINK";
        }

        return "BLUE";
    }

}  // namespace

void TwitchGql::getActivePrediction(
    const QString &channelLogin, const QString &oauthToken,
    std::function<void(std::optional<TwitchChannel::PredictionEvent>)>
        successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    variables.insert("channelLogin", channelLogin);

    static const char *predictionQuery = R"(
        query ChannelPointsPredictionContext($channelLogin: String!) {
            channel(name: $channelLogin) {
                id
                activePredictionEvents {
                    id
                    title
                    status
                    predictionWindowSeconds
                    createdAt
                    lockedAt
                    outcomes {
                        id
                        title
                        totalUsers
                        totalPoints
                        color
                        topPredictors {
                            points
                            user {
                                displayName
                                login
                            }
                        }
                    }
                    createdBy { ... on User { displayName login } }
                    lockedBy { ... on User { displayName login } }
                    endedBy { ... on User { displayName login } }
                }
                lockedPredictionEvents {
                    id
                    title
                    status
                    predictionWindowSeconds
                    createdAt
                    lockedAt
                    outcomes {
                        id
                        title
                        totalUsers
                        totalPoints
                        color
                        topPredictors {
                            points
                            user {
                                displayName
                                login
                            }
                        }
                    }
                    createdBy { ... on User { displayName login } }
                    lockedBy { ... on User { displayName login } }
                    endedBy { ... on User { displayName login } }
                }
            }
        }
    )";

    makeInlineGqlRequest(predictionQuery, variables, oauthToken)
        .onSuccess([successCallback,
                    failureCallback](const NetworkResult &result) {
            auto doc = result.parseRapidJson();
            if (doc.HasParseError())
            {
                failureCallback("Failed to parse GQL response");
                return;
            }

            const auto *dataVal = gqlData(doc);
            if (dataVal == nullptr || !dataVal->HasMember("channel") ||
                !(*dataVal)["channel"].IsObject())
            {
                successCallback(std::nullopt);
                return;
            }

            auto channel = (*dataVal)["channel"].GetObject();
            const rapidjson::Value *nodePtr = nullptr;

            if (channel.HasMember("activePredictionEvents") &&
                channel["activePredictionEvents"].IsArray())
            {
                const auto &activeEvents = channel["activePredictionEvents"];
                if (!activeEvents.Empty() && activeEvents[0].IsObject())
                {
                    nodePtr = &activeEvents[0];
                }
            }

            if (nodePtr == nullptr &&
                channel.HasMember("lockedPredictionEvents") &&
                channel["lockedPredictionEvents"].IsArray())
            {
                const auto &lockedEvents = channel["lockedPredictionEvents"];
                if (!lockedEvents.Empty() && lockedEvents[0].IsObject())
                {
                    nodePtr = &lockedEvents[0];
                }
            }

            if (nodePtr == nullptr)
            {
                successCallback(std::nullopt);
                return;
            }

            const auto &node = *nodePtr;
            TwitchChannel::PredictionEvent prediction;
            rj::getSafe(node, "id", prediction.id);
            rj::getSafe(node, "title", prediction.title);
            rj::getSafe(node, "status", prediction.status);
            rj::getSafe(node, "predictionWindowSeconds",
                        prediction.predictionWindowSeconds);

            if (prediction.status.compare("ACTIVE", Qt::CaseInsensitive) != 0 &&
                prediction.status.compare("LOCKED", Qt::CaseInsensitive) != 0)
            {
                successCallback(std::nullopt);
                return;
            }

            QString createdAtStr;
            if (rj::getSafe(node, "createdAt", createdAtStr) &&
                !createdAtStr.isEmpty())
            {
                prediction.createdAt =
                    QDateTime::fromString(createdAtStr, Qt::ISODate);
            }

            QString lockedAtStr;
            if (rj::getSafe(node, "lockedAt", lockedAtStr) &&
                !lockedAtStr.isEmpty())
            {
                prediction.lockedAt =
                    QDateTime::fromString(lockedAtStr, Qt::ISODate);
            }

            if (node.HasMember("createdBy") && node["createdBy"].IsObject())
            {
                rj::getSafe(node["createdBy"], "displayName",
                            prediction.createdByName);
                if (prediction.createdByName.isEmpty())
                {
                    rj::getSafe(node["createdBy"], "login",
                                prediction.createdByName);
                }
            }
            if (node.HasMember("lockedBy") && node["lockedBy"].IsObject())
            {
                rj::getSafe(node["lockedBy"], "displayName",
                            prediction.lockedByName);
                if (prediction.lockedByName.isEmpty())
                {
                    rj::getSafe(node["lockedBy"], "login",
                                prediction.lockedByName);
                }
            }
            if (node.HasMember("endedBy") && node["endedBy"].IsObject())
            {
                rj::getSafe(node["endedBy"], "displayName",
                            prediction.endedByName);
                if (prediction.endedByName.isEmpty())
                {
                    rj::getSafe(node["endedBy"], "login",
                                prediction.endedByName);
                }
            }

            if (node.HasMember("self") && node["self"].IsObject())
            {
                const auto &self = node["self"];
                rj::getSafe(self, "pointsParticipated", prediction.selfPoints);
                if (self.HasMember("outcome") && self["outcome"].IsObject())
                {
                    rj::getSafe(self["outcome"], "id",
                                prediction.selfOutcomeId);
                }
            }

            if (node.HasMember("outcomes") && node["outcomes"].IsArray())
            {
                const auto &outcomesArr = node["outcomes"];
                int outcomeCount = outcomesArr.Size();
                for (int i = 0; i < outcomeCount; ++i)
                {
                    if (!outcomesArr[i].IsObject())
                        continue;
                    const auto &oObj = outcomesArr[i];
                    TwitchChannel::PredictionOutcome outcome;
                    rj::getSafe(oObj, "id", outcome.id);
                    rj::getSafe(oObj, "title", outcome.title);
                    rj::getSafe(oObj, "totalUsers", outcome.totalUsers);

                    if (oObj.HasMember("totalPoints") &&
                        oObj["totalPoints"].IsNumber())
                    {
                        outcome.totalPoints = static_cast<qlonglong>(
                            oObj["totalPoints"].GetInt64());
                    }

                    if (outcomeCount == 2)
                        outcome.color = (i == 0) ? "BLUE" : "PINK";
                    else if (outcomeCount == 3)
                        outcome.color =
                            (i == 0) ? "BLUE" : (i == 1 ? "PINK" : "GREEN");
                    else
                        outcome.color = "BLUE";

                    if (oObj.HasMember("topPredictors") &&
                        oObj["topPredictors"].IsArray())
                    {
                        const auto &predictors =
                            oObj["topPredictors"].GetArray();
                        if (predictors.Size() > 0 && predictors[0].IsObject())
                        {
                            const auto &top = predictors[0];
                            if (top.HasMember("points") &&
                                top["points"].IsNumber())
                            {
                                outcome.topPoints = static_cast<qlonglong>(
                                    top["points"].GetInt64());
                            }
                            if (top.HasMember("user") &&
                                top["user"].IsObject())
                            {
                                rj::getSafe(top["user"], "displayName",
                                            outcome.topPredictorName);
                                if (outcome.topPredictorName.isEmpty())
                                {
                                    rj::getSafe(top["user"], "login",
                                                outcome.topPredictorName);
                                }
                            }
                        }
                    }

                    prediction.outcomes.push_back(std::move(outcome));
                }
            }

            successCallback(prediction);
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::makePrediction(
    const QString &eventID, const QString &outcomeID, int points,
    const QString &oauthToken, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    QJsonObject input;
    input.insert("eventID", eventID);
    input.insert("outcomeID", outcomeID);
    input.insert("points", points);

    auto uuid = generateUuid();
    uuid.remove('{').remove('}').remove('-');
    input.insert("transactionID", uuid);

    variables.insert("input", input);

    makePersistedGqlRequest(
        "MakePrediction",
        "b44682ecc88358817009f20e69d75081b1e58825bb40aa53d5dbadcc17c881d8",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            QString errorMessage;
            if (!checkMutationResponse(doc, "makePrediction", true,
                                       errorMessage))
            {
                failureCallback(
                    errorMessage.isEmpty()
                        ? "Twitch API Error: Failed to place prediction"
                        : "Twitch API Error: " + errorMessage);
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::createPredictionEvent(
    const QString &channelId, const QString &title,
    const QStringList &outcomes, int predictionWindowSeconds,
    const QString &oauthToken, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    QJsonObject input;
    input.insert("channelID", channelId);
    input.insert("title", title);
    input.insert("predictionWindowSeconds", predictionWindowSeconds);

    QJsonArray outcomesArray;
    const int outcomeCount = outcomes.size();
    for (int i = 0; i < outcomeCount; ++i)
    {
        QJsonObject outcome;
        outcome.insert("title", outcomes.at(i));
        outcome.insert("color",
                       predictionCreateOutcomeColor(i, outcomeCount));
        outcomesArray.append(outcome);
    }
    input.insert("outcomes", outcomesArray);
    variables.insert("input", input);

    makePersistedGqlRequest(
        "createPredictionEvent",
        "92268878ac4abe722bcdcba85a4e43acdd7a99d86b05851759e1d8f385cc32ea",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            // Additionally verify a predictionEvent id came back
            QString errorMessage;
            if (!checkMutationResponse(doc, "createPredictionEvent", true,
                                       errorMessage))
            {
                failureCallback(
                    errorMessage.isEmpty()
                        ? "Twitch API Error: Failed to create prediction"
                        : "Twitch API Error: " + errorMessage);
                return;
            }

            const auto *data = gqlData(doc);
            const auto &payload = (*data)["createPredictionEvent"];
            if (!payload.HasMember("predictionEvent") ||
                !payload["predictionEvent"].IsObject() ||
                !payload["predictionEvent"].HasMember("id") ||
                !payload["predictionEvent"]["id"].IsString() ||
                QString::fromUtf8(
                    payload["predictionEvent"]["id"].GetString())
                    .isEmpty())
            {
                failureCallback(
                    "Twitch API Error: Failed to create prediction");
                return;
            }

            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::getPredictionTemplates(
    const QString &channelLogin, const QString &oauthToken,
    std::function<void(QVector<PredictionTemplate>)> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    variables.insert("count", 5);
    variables.insert("channelLogin", channelLogin);

    makePersistedGqlRequest(
        "ChannelPointsPredictionContext",
        "beb846598256b75bd7c1fe54a80431335996153e358ca9c7837ce7bb83d7d383",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();
            if (doc.HasParseError())
            {
                failureCallback("Failed to parse GQL response");
                return;
            }

            if (const auto error = gqlFirstErrorMessage(doc);
                !error.isEmpty())
            {
                failureCallback(error);
                return;
            }

            const auto *dataVal = gqlData(doc);
            if (dataVal == nullptr || !dataVal->HasMember("community") ||
                !(*dataVal)["community"].IsObject())
            {
                failureCallback("Missing prediction history");
                return;
            }

            const auto &community = (*dataVal)["community"];
            if (!community.HasMember("channel") ||
                !community["channel"].IsObject())
            {
                failureCallback("Missing prediction channel");
                return;
            }

            const auto &channel = community["channel"];
            if (!channel.HasMember("resolvedPredictionEvents") ||
                !channel["resolvedPredictionEvents"].IsObject())
            {
                successCallback({});
                return;
            }

            const auto &connection = channel["resolvedPredictionEvents"];
            if (!connection.HasMember("edges") ||
                !connection["edges"].IsArray())
            {
                successCallback({});
                return;
            }

            QVector<PredictionTemplate> templates;
            templates.reserve(5);

            for (const auto &edge : connection["edges"].GetArray())
            {
                if (!edge.IsObject() || !edge.HasMember("node") ||
                    !edge["node"].IsObject())
                {
                    continue;
                }

                const auto &node = edge["node"];
                PredictionTemplate predictionTemplate;
                rj::getSafe(node, "title", predictionTemplate.title);
                rj::getSafe(node, "predictionWindowSeconds",
                            predictionTemplate.durationSeconds);

                if (predictionTemplate.title.trimmed().isEmpty() ||
                    !node.HasMember("outcomes") ||
                    !node["outcomes"].IsArray())
                {
                    continue;
                }

                for (const auto &outcome : node["outcomes"].GetArray())
                {
                    if (!outcome.IsObject())
                    {
                        continue;
                    }
                    QString outcomeTitle;
                    if (rj::getSafe(outcome, "title", outcomeTitle) &&
                        !outcomeTitle.trimmed().isEmpty())
                    {
                        predictionTemplate.outcomes.append(outcomeTitle);
                    }
                }

                if (predictionTemplate.outcomes.size() >= 2)
                {
                    templates.append(std::move(predictionTemplate));
                }
            }

            successCallback(templates);
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::lockPrediction(
    const QString &eventId, const QString &oauthToken,
    std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    QJsonObject input;
    input.insert("id", eventId);
    variables.insert("input", input);

    makePersistedGqlRequest(
        "LockPrediction",
        "1f2b1eb44af35f055308e78ffbe81c2f958408f9b32d076a759a84ab213285d4",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            QString errorMessage;
            if (!checkMutationResponse(doc, "lockPrediction", false,
                                       errorMessage))
            {
                failureCallback("Twitch API Error: Failed to lock prediction");
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::cancelPrediction(
    const QString &eventId, const QString &oauthToken,
    std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    QJsonObject input;
    input.insert("id", eventId);
    variables.insert("input", input);

    makePersistedGqlRequest(
        "DeletePrediction",
        "35d375614e426624456ee7be4a2e0fbc0a410c0a91c21f6044cb3cd5c38c4e4d",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            // The mutation must return a payload object whose error is null.
            QString errorMessage;
            if (!checkMutationResponse(doc, "cancelPredictionEvent", true,
                                       errorMessage))
            {
                failureCallback(
                    "Twitch API Error: Failed to delete prediction");
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::resolvePrediction(
    const QString &eventId, const QString &outcomeId,
    const QString &oauthToken, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    QJsonObject input;
    input.insert("eventID", eventId);
    input.insert("outcomeID", outcomeId);
    variables.insert("input", input);

    makePersistedGqlRequest(
        "ResolvePrediction",
        "10c803ec11bb8c2957d66bc6a47349dc3c5f51d694585b5ebc37ba656da413c1",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            // Resolve only surfaces top-level GQL errors; Twitch does not
            // consistently return a data payload for this mutation.
            if (const auto error = gqlFirstErrorMessage(doc);
                !error.isEmpty())
            {
                failureCallback(
                    "Twitch API Error: Failed to resolve prediction");
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::getChannelPoints(
    const QString &channelLogin, const QString &oauthToken,
    std::function<void(qint64)> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    variables.insert("channelLogin", channelLogin);

    static const char *channelPointsQuery = R"(
        query ChannelPointsContext($channelLogin: String!) {
            channel(name: $channelLogin) {
                self {
                    communityPoints {
                        balance
                    }
                }
            }
        }
    )";

    makeInlineGqlRequest(channelPointsQuery, variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();
            if (doc.HasParseError())
            {
                failureCallback("Failed to parse GQL response");
                return;
            }

            const auto *dataVal = gqlData(doc);
            if (dataVal == nullptr)
            {
                failureCallback("Could not parse channel points balance");
                return;
            }

            if (dataVal->HasMember("channel") &&
                (*dataVal)["channel"].IsObject())
            {
                const auto &channel = (*dataVal)["channel"];
                if (channel.HasMember("self") && channel["self"].IsObject())
                {
                    const auto &self = channel["self"];
                    if (self.HasMember("communityPoints") &&
                        self["communityPoints"].IsObject())
                    {
                        const auto &cp = self["communityPoints"];
                        if (cp.HasMember("balance") &&
                            cp["balance"].IsInt64())
                        {
                            successCallback(cp["balance"].GetInt64());
                            return;
                        }
                    }
                }
            }

            failureCallback("Could not parse channel points balance");
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

// ---------------------------------------------------------------------------
// Blocked terms, user lookup and lead-moderator role mutations - ported from
// Moltorino for the /blockterm, /unblockterm, /leadmod and /unleadmod
// commands.
// ---------------------------------------------------------------------------

void TwitchGql::addChannelBlockedTerm(
    const QString &channelId, const QString &phrase, const QString &oauthToken,
    std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject input;
    input.insert("channelID", channelId);
    input.insert("phrase", phrase);
    input.insert("phrases", QJsonArray{phrase});
    input.insert("isModEditable", true);

    QJsonObject variables;
    variables.insert("input", input);

    makePersistedGqlRequest(
        "AddChannelBlockedTerm",
        "10f4c5c8dd6817c21058040b50181040e91e894ca324b14beda6b5f5e429aa02",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            QString errorMessage;
            if (!checkMutationResponse(doc, "addChannelBlockedTerm", true,
                                       errorMessage))
            {
                failureCallback(
                    errorMessage.isEmpty()
                        ? "Twitch API Error: Failed to add blocked term"
                        : "Twitch API Error: " + errorMessage);
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::getChannelBlockedTerms(
    const QString &channelId, const QString &oauthToken,
    std::function<void(QVector<GqlBlockedTerm>)> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    variables.insert("channelID", channelId);

    makePersistedGqlRequest(
        "BlockedTerms",
        "022dc6d166de51129700aa03482dca9e5fffc3a7045ba7f1deeaa3046a39577f",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            const auto topError = gqlFirstErrorMessage(doc);
            if (!topError.isEmpty())
            {
                failureCallback("Twitch API Error: " + topError);
                return;
            }

            const auto *data = gqlData(doc);
            if (data == nullptr || !data->HasMember("channel") ||
                !(*data)["channel"].IsObject())
            {
                failureCallback(
                    "Twitch API Error: Failed to fetch blocked terms");
                return;
            }

            const auto &channel = (*data)["channel"];
            if (!channel.HasMember("blockedTerms") ||
                !channel["blockedTerms"].IsObject())
            {
                failureCallback(
                    "Twitch API Error: Failed to fetch blocked terms");
                return;
            }

            QVector<GqlBlockedTerm> terms;
            const auto &blockedTerms = channel["blockedTerms"];

            auto appendTerm = [&terms](const rapidjson::Value &node) {
                if (!node.IsObject())
                {
                    return;
                }
                GqlBlockedTerm term;
                if (rj::getSafe(node, "id", term.id) &&
                    rj::getSafe(node, "phrase", term.phrase) &&
                    !term.id.isEmpty() && !term.phrase.isEmpty())
                {
                    rj::getSafe(node, "expiresAt", term.expiresAt);
                    terms.push_back(std::move(term));
                }
            };

            if (blockedTerms.HasMember("edges") &&
                blockedTerms["edges"].IsArray())
            {
                for (const auto &edge : blockedTerms["edges"].GetArray())
                {
                    if (edge.IsObject() && edge.HasMember("node"))
                    {
                        appendTerm(edge["node"]);
                    }
                }
            }

            if (blockedTerms.HasMember("nodes") &&
                blockedTerms["nodes"].IsArray())
            {
                for (const auto &node : blockedTerms["nodes"].GetArray())
                {
                    appendTerm(node);
                }
            }

            successCallback(std::move(terms));
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::deleteChannelBlockedTerm(
    const QString &channelId, const QString &termId, const QString &oauthToken,
    std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject input;
    input.insert("id", termId);
    input.insert("channelID", channelId);

    QJsonObject variables;
    variables.insert("input", input);

    makePersistedGqlRequest(
        "DeleteChannelBlockedTerm",
        "bdfacf843eb536eef2720110cf73a4540506833b17a3f15313e461e57165c813",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            QString errorMessage;
            if (!checkMutationResponse(doc, "deleteChannelBlockedTermByID",
                                       true, errorMessage))
            {
                failureCallback(
                    errorMessage.isEmpty()
                        ? "Twitch API Error: Failed to remove blocked term"
                        : "Twitch API Error: " + errorMessage);
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::getUserByLogin(
    const QString &login, const QString &oauthToken,
    std::function<void(std::optional<GqlUser>)> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    static constexpr char QUERY[] = R"(
        query ChatterinoUserByLogin($login: String!) {
            user(login: $login) {
                id
                login
                displayName
            }
        }
    )";

    QJsonObject variables;
    variables.insert("login", login);

    makeInlineGqlRequest(QUERY, variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            const auto topError = gqlFirstErrorMessage(doc);
            if (!topError.isEmpty())
            {
                failureCallback("Twitch API Error: " + topError);
                return;
            }

            const auto *data = gqlData(doc);
            if (data == nullptr || !data->HasMember("user") ||
                !(*data)["user"].IsObject())
            {
                failureCallback("Twitch API Error: Missing user data");
                return;
            }

            GqlUser user;
            const auto &node = (*data)["user"];
            if (!rj::getSafe(node, "id", user.id) || user.id.isEmpty())
            {
                // A missing user is not an error for our callers.
                successCallback(std::nullopt);
                return;
            }
            rj::getSafe(node, "login", user.login);
            rj::getSafe(node, "displayName", user.displayName);
            successCallback(std::move(user));
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

namespace {

/// Shared implementation for the lead-moderator role mutations (persisted
/// query + input{channelID, targetUserID} + payload error object).
void runLeadModRoleMutation(
    const QString &operationName, const QString &hash,
    const QString &payloadName, const QString &channelId,
    const QString &targetUserId, const QString &oauthToken,
    const QString &fallbackError, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject input;
    input.insert("channelID", channelId);
    input.insert("targetUserID", targetUserId);

    QJsonObject variables;
    variables.insert("input", input);

    makePersistedGqlRequest(operationName, hash, variables, oauthToken)
        .onSuccess([payloadName, fallbackError, successCallback,
                    failureCallback](const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            // Keep the byte array alive for the duration of the call.
            const QByteArray payloadKey = payloadName.toUtf8();

            QString errorMessage;
            if (!checkMutationResponse(doc, payloadKey.constData(), true,
                                       errorMessage))
            {
                failureCallback(
                    errorMessage.isEmpty()
                        ? "Twitch API Error: " + fallbackError
                        : "Twitch API Error: " + errorMessage);
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

}  // namespace

void TwitchGql::assignLeadModerator(
    const QString &channelId, const QString &targetUserId,
    const QString &oauthToken, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    runLeadModRoleMutation(
        "AssignChannelRole",
        "2d373c90d0d0e6d4fe771bc6136febe6a148eb3d5700d2a0575883a043fbd581",
        "assignChannelRole", channelId, targetUserId, oauthToken,
        "Failed to add lead moderator", std::move(successCallback),
        std::move(failureCallback));
}

void TwitchGql::unassignLeadModerator(
    const QString &channelId, const QString &targetUserId,
    const QString &oauthToken, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    runLeadModRoleMutation(
        "UnassignChannelRole",
        "5edbf17877acdb91e65243b5148cfd15b98adc6d8f980492dcde9a7f2e8255e2",
        "unassignChannelRole", channelId, targetUserId, oauthToken,
        "Failed to remove lead moderator", std::move(successCallback),
        std::move(failureCallback));
}

// ---------------------------------------------------------------------------
// Channel editors (/editor, /uneditor) - TV-client mutations that take the
// target's login name. Ported from Moltorino's runTvRoleMutation.
// ---------------------------------------------------------------------------

namespace {

/// Shared implementation for TV-client role mutations (persisted query +
/// input{channelID, targetUserLogin} + payload error object).
void runEditorRoleMutation(
    const QString &operationName, const QString &hash,
    const QString &payloadName, const QString &channelId,
    const QString &targetLogin, const QString &oauthToken,
    const QString &fallbackError, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject input;
    input.insert("channelID", channelId);
    input.insert("targetUserLogin", targetLogin);

    QJsonObject variables;
    variables.insert("input", input);

    makeTvPersistedGqlRequest(operationName, hash, variables, oauthToken)
        .onSuccess([operationName, payloadName, fallbackError,
                    successCallback,
                    failureCallback](const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            // Keep the byte array alive for the duration of the call.
            const QByteArray payloadKey = payloadName.toUtf8();

            QString errorMessage;
            if (!checkTvMutationResponse(doc, operationName,
                                         payloadKey.constData(), nullptr,
                                         fallbackError, errorMessage))
            {
                failureCallback("Twitch API Error: " + errorMessage);
                return;
            }
            successCallback();
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

}  // namespace

void TwitchGql::addEditorUser(
    const QString &channelId, const QString &targetLogin,
    const QString &oauthToken, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    runEditorRoleMutation(
        "AddEditorUser",
        "3b52bf904ff9ce1b000ac2358080f538fbd1972c1869804f0d0f345d1a56676c",
        "addEditor", channelId, targetLogin, oauthToken,
        "Failed to add editor", std::move(successCallback),
        std::move(failureCallback));
}

void TwitchGql::removeEditorUser(
    const QString &channelId, const QString &targetLogin,
    const QString &oauthToken, std::function<void()> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    runEditorRoleMutation(
        "RemoveEditorUser",
        "4699d38183050854dba547d07e340e72bf1f04578f1037a38a1189fa1827790f",
        "removeEditor", channelId, targetLogin, oauthToken,
        "Failed to remove editor", std::move(successCallback),
        std::move(failureCallback));
}

// ---------------------------------------------------------------------------
// Channel point rewards (/redeem) - ported from Moltorino's
// getChannelPointRewards and redeemCustomReward.
// ---------------------------------------------------------------------------

namespace {

qint64 rewardCostFromValue(const rapidjson::Value &obj)
{
    if (obj.HasMember("cost") && obj["cost"].IsInt() && obj["cost"].GetInt() > 0)
    {
        return obj["cost"].GetInt();
    }
    if (obj.HasMember("defaultCost") && obj["defaultCost"].IsInt())
    {
        return obj["defaultCost"].GetInt();
    }
    return 0;
}

QString automaticRewardTitle(const QString &type)
{
    static const QHash<QString, QString> titles{
        {"RANDOM_SUB_EMOTE_UNLOCK", "Unlock a Random Emote"},
        {"CHOSEN_SUB_EMOTE_UNLOCK", "Choose an Emote to Unlock"},
        {"CHOSEN_MODIFIED_SUB_EMOTE_UNLOCK", "Modify a Single Emote"},
        {"SINGLE_MESSAGE_BYPASS_SUB_MODE", "Send a Message in Sub-Only"},
        {"SEND_HIGHLIGHTED_MESSAGE", "Highlight My Message"},
        {"SEND_ANIMATED_MESSAGE", "Message Effects"},
        {"SEND_GIGANTIFIED_EMOTE", "Gigantify an Emote"},
        {"CELEBRATION", "On-Screen Celebration"},
    };
    return titles.value(type, type);
}

QString automaticRewardPrompt(const QString &type)
{
    static const QHash<QString, QString> prompts{
        {"RANDOM_SUB_EMOTE_UNLOCK",
         "Unlock a random subscriber emote for 24 hours."},
        {"CHOSEN_SUB_EMOTE_UNLOCK",
         "Pick a subscriber emote to unlock for 24 hours."},
        {"CHOSEN_MODIFIED_SUB_EMOTE_UNLOCK",
         "Pick an emote and modifier to unlock for 24 hours."},
        {"SINGLE_MESSAGE_BYPASS_SUB_MODE",
         "Send one message while sub-only mode is active."},
        {"SEND_HIGHLIGHTED_MESSAGE", "Send one highlighted message."},
    };
    return prompts.value(type);
}

GqlChannelPointReward channelPointRewardFromValue(
    const rapidjson::Value &obj, bool automatic)
{
    GqlChannelPointReward reward;
    reward.isAutomatic = automatic;
    rj::getSafe(obj, "id", reward.id);
    QString type;
    if (automatic && rj::getSafe(obj, "type", type))
    {
        reward.rewardType = type;
    }
    else
    {
        reward.rewardType = QStringLiteral("CUSTOM_REWARD");
    }
    if (automatic)
    {
        reward.title = automaticRewardTitle(reward.rewardType);
        reward.prompt = automaticRewardPrompt(reward.rewardType);
    }
    else
    {
        rj::getSafe(obj, "title", reward.title);
        rj::getSafe(obj, "prompt", reward.prompt);
    }
    if (!rj::getSafe(obj, "pricingType", reward.pricingType) ||
        reward.pricingType.isEmpty())
    {
        reward.pricingType = QStringLiteral("POINTS");
    }
    reward.cost = rewardCostFromValue(obj);

    bool isEnabled = false;
    bool isPaused = false;
    rj::getSafe(obj, "isEnabled", isEnabled);
    rj::getSafe(obj, "isPaused", isPaused);
    reward.isEnabled = isEnabled && !isPaused;

    reward.isInStock = true;
    rj::getSafe(obj, "isInStock", reward.isInStock);

    reward.isUserInputRequired = false;
    rj::getSafe(obj, "isUserInputRequired", reward.isUserInputRequired);
    return reward;
}

}  // namespace

void TwitchGql::getChannelPointRewards(
    const QString &channelLogin, const QString &oauthToken,
    std::function<void(GqlChannelPointRewards)> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject variables;
    variables.insert("channelLogin", channelLogin);
    variables.insert("includeGoalTypes",
                     QJsonArray{QStringLiteral("CREATOR"),
                                QStringLiteral("BOOST")});

    makeTvPersistedGqlRequest(
        "ChannelPointsContext",
        "7fe050e3761eb2cf258d70ee1a21cbd76fa8cf3d7e7b12fc437e7029d446b5e3",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();
            if (doc.HasParseError())
            {
                failureCallback("Failed to parse GQL response");
                return;
            }

            if (const auto error = gqlFirstErrorMessage(doc);
                !error.isEmpty())
            {
                failureCallback("Twitch API Error: " + error);
                return;
            }

            const auto *dataVal = gqlDataForOperation(
                doc, QStringLiteral("ChannelPointsContext"));
            if (dataVal == nullptr || !dataVal->HasMember("community") ||
                !(*dataVal)["community"].IsObject())
            {
                failureCallback("Channel point rewards are unavailable");
                return;
            }

            const auto &community = (*dataVal)["community"];
            if (!community.HasMember("channel") ||
                !community["channel"].IsObject())
            {
                failureCallback("Channel point rewards are unavailable");
                return;
            }

            const auto &channel = community["channel"];
            if (!channel.HasMember("communityPointsSettings") ||
                !channel["communityPointsSettings"].IsObject())
            {
                failureCallback("Channel point rewards are unavailable");
                return;
            }

            const auto &settings = channel["communityPointsSettings"];

            GqlChannelPointRewards rewards;
            rj::getSafe(community, "id", rewards.channelId);
            rj::getSafe(community, "displayName", rewards.channelDisplayName);

            if (channel.HasMember("self") && channel["self"].IsObject())
            {
                const auto &self = channel["self"];
                if (self.HasMember("communityPoints") &&
                    self["communityPoints"].IsObject() &&
                    self["communityPoints"].HasMember("balance") &&
                    self["communityPoints"]["balance"].IsInt64())
                {
                    rewards.balance =
                        self["communityPoints"]["balance"].GetInt64();
                }
            }

            if (settings.HasMember("customRewards") &&
                settings["customRewards"].IsArray())
            {
                for (const auto &value :
                     settings["customRewards"].GetArray())
                {
                    if (!value.IsObject())
                    {
                        continue;
                    }
                    auto reward =
                        channelPointRewardFromValue(value, false);
                    if (reward.pricingType != "POINTS" || reward.cost <= 0)
                    {
                        continue;
                    }
                    rewards.rewards.push_back(std::move(reward));
                }
            }

            if (settings.HasMember("automaticRewards") &&
                settings["automaticRewards"].IsArray())
            {
                for (const auto &value :
                     settings["automaticRewards"].GetArray())
                {
                    if (!value.IsObject())
                    {
                        continue;
                    }
                    auto reward =
                        channelPointRewardFromValue(value, true);
                    if (reward.pricingType != "POINTS" || reward.cost <= 0)
                    {
                        continue;
                    }
                    rewards.rewards.push_back(std::move(reward));
                }
            }

            successCallback(std::move(rewards));
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

void TwitchGql::redeemChannelPointReward(
    const QString &channelId, const GqlChannelPointReward &reward,
    const QString &textInput, const QString &oauthToken,
    std::function<void(qint64)> successCallback,
    std::function<void(const QString &)> failureCallback)
{
    QJsonObject input;
    input.insert("channelID", channelId);
    input.insert("cost", reward.cost);
    input.insert("pricingType", "POINTS");
    input.insert("rewardID", reward.id);
    input.insert("title", reward.title);
    input.insert("transactionID", generateUuid().remove('{').remove('}').remove('-'));
    if (reward.prompt.trimmed().isEmpty())
    {
        input.insert("prompt", QJsonValue(QJsonValue::Null));
    }
    else
    {
        input.insert("prompt", reward.prompt);
    }
    if (!textInput.trimmed().isEmpty())
    {
        input.insert("textInput", textInput);
    }

    QJsonObject variables;
    variables.insert("input", input);

    makeTvPersistedGqlRequest(
        "RedeemCustomReward",
        "d56249a7adb4978898ea3412e196688d4ac3cea1c0c2dfd65561d229ea5dcc42",
        variables, oauthToken)
        .onSuccess([successCallback, failureCallback](
                       const NetworkResult &result) {
            auto doc = result.parseRapidJson();

            QString errorMessage;
            if (!checkTvMutationResponse(
                    doc, QStringLiteral("RedeemCustomReward"),
                    "redeemCommunityPointsCustomReward", nullptr,
                    "Failed to redeem reward", errorMessage))
            {
                failureCallback("Twitch API Error: " + errorMessage);
                return;
            }

            const auto *data = gqlDataForOperation(
                doc, QStringLiteral("RedeemCustomReward"));
            qint64 balance = -1;
            if (data != nullptr)
            {
                const auto &payload =
                    (*data)["redeemCommunityPointsCustomReward"];
                if (payload.HasMember("balance") &&
                    payload["balance"].IsInt64())
                {
                    balance = payload["balance"].GetInt64();
                }
            }

            successCallback(balance);
        })
        .onError([failureCallback](const NetworkResult &result) {
            failureCallback("Network Error: " +
                            QString::number(result.status().value_or(0)));
        })
        .execute();
}

}  // namespace chatterino
