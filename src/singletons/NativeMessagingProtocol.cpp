// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/NativeMessagingProtocol.hpp"

#include "common/Literals.hpp"

#include <cmath>

namespace chatterino::nm {

using namespace chatterino::literals;
constexpr double MAX_SAFE_JSON_INTEGER = 9007199254740991.0;

ParsedNativeMessage parseNativeMessage(const QJsonObject &root)
{
    ParsedNativeMessage parsed;
    const auto versionValue = root["protocolVersion"_L1];
    if (!versionValue.isUndefined())
    {
        if (!versionValue.isDouble())
        {
            parsed.error = u"protocolVersion must be an integer"_s;
            return parsed;
        }
        const auto numericVersion = versionValue.toDouble(-1);
        if (!std::isfinite(numericVersion) ||
            std::floor(numericVersion) != numericVersion ||
            numericVersion < 0 || numericVersion > CURRENT_PROTOCOL_VERSION)
        {
            parsed.error = u"unsupported protocol version"_s;
            return parsed;
        }
        parsed.protocolVersion = static_cast<int>(numericVersion);
    }

    if (parsed.protocolVersion < 0 ||
        parsed.protocolVersion > CURRENT_PROTOCOL_VERSION)
    {
        parsed.error =
            u"unsupported protocol version %1"_s.arg(parsed.protocolVersion);
        return parsed;
    }

    const auto action = root["action"_L1].toString();
    if (action == u"select")
        parsed.action = NativeAction::Select;
    else if (action == u"detach")
        parsed.action = NativeAction::Detach;
    else if (action == u"sync")
        parsed.action = NativeAction::Sync;
    else if (action == u"engagement")
        parsed.action = NativeAction::Engagement;
    else if (action == u"prediction")
        parsed.action = NativeAction::PredictionLegacy;
    else if (action == u"pin")
        parsed.action = NativeAction::Pin;
    else if (action == u"rewardPending")
        parsed.action = NativeAction::RewardPending;
    else if (action == u"rewardClear")
        parsed.action = NativeAction::RewardClear;
    else if (action == u"leaseRenew")
        parsed.action = NativeAction::LeaseRenew;
    else if (action == u"reconcile")
        parsed.action = NativeAction::Reconcile;
    else if (action == u"nativeChatResult")
        parsed.action = NativeAction::NativeChatResult;
    else
    {
        parsed.error = action.isEmpty() ? u"missing action"_s
                                        : u"unknown action: %1"_s.arg(action);
        return parsed;
    }

    if (parsed.action == NativeAction::Engagement &&
        parsed.protocolVersion >= 1)
    {
        const auto kind = root["kind"_L1].toString();
        const auto lifecycle = root["lifecycle"_L1].toString();
        if (kind != u"poll" && kind != u"prediction")
        {
            parsed.error = u"invalid engagement kind"_s;
        }
        else if (lifecycle != u"upsert" && lifecycle != u"remove")
        {
            parsed.error = u"invalid engagement lifecycle"_s;
        }
        else if (root["channel"_L1].toString().trimmed().isEmpty())
        {
            parsed.error = u"engagement channel is required"_s;
        }
        else if (lifecycle == u"upsert" &&
                 root["title"_L1].toString().trimmed().isEmpty())
        {
            parsed.error = u"engagement title is required for upsert"_s;
        }
    }

    if (parsed.protocolVersion >= 2)
    {
        const bool sessionAction =
            parsed.action == NativeAction::Select ||
            parsed.action == NativeAction::Detach ||
            parsed.action == NativeAction::LeaseRenew ||
            parsed.action == NativeAction::NativeChatResult;
        if (sessionAction && !parseAttachmentIdentity(root).isComplete())
        {
            parsed.error = u"v2 session identity is incomplete"_s;
            return parsed;
        }

        const auto requestId = root["requestId"_L1].toString(
            root["attachRequestId"_L1].toString());
        if (requestId.size() > 256)
        {
            parsed.error = u"v2 request identity is too long"_s;
            return parsed;
        }

        if (parsed.action == NativeAction::Select)
        {
            const auto size = root["size"_L1].toObject();
            const auto width = size["width"_L1];
            const auto height = size["height"_L1];
            const auto x = size["x"_L1];
            const auto pixelRatio = size["pixelRatio"_L1];
            if (!width.isDouble() || !height.isDouble() ||
                std::floor(width.toDouble()) != width.toDouble() ||
                std::floor(height.toDouble()) != height.toDouble() ||
                width.toDouble() < 1 || width.toDouble() > 32768 ||
                height.toDouble() < 1 || height.toDouble() > 32768 ||
                (x.isDouble() && !std::isfinite(x.toDouble())) ||
                (pixelRatio.isDouble() &&
                 (!std::isfinite(pixelRatio.toDouble()) ||
                  pixelRatio.toDouble() <= 0 || pixelRatio.toDouble() > 16)))
            {
                parsed.error = u"v2 select geometry is out of range"_s;
                return parsed;
            }
            const auto browserHwnd = root["browserHwnd"_L1].toString();
            if (browserHwnd.size() > 32)
            {
                parsed.error = u"v2 select browserHwnd is too long"_s;
                return parsed;
            }
            if (root["leaseExpiresAt"_L1].toVariant().toLongLong() <= 0 &&
                root["leaseDurationMs"_L1].toVariant().toLongLong() <= 0 &&
                root["leaseMs"_L1].toVariant().toLongLong() <= 0)
            {
                parsed.error = u"v2 select requires a lease"_s;
                return parsed;
            }
        }

        if (parsed.action == NativeAction::LeaseRenew &&
            root["leaseExpiresAt"_L1].toVariant().toLongLong() <= 0 &&
            root["leaseDurationMs"_L1].toVariant().toLongLong() <= 0 &&
            root["leaseMs"_L1].toVariant().toLongLong() <= 0)
        {
            parsed.error = u"v2 lease renewal requires leaseExpiresAt"_s;
            return parsed;
        }

        if (parsed.action == NativeAction::NativeChatResult)
        {
            const auto status = root["status"_L1].toString();
            if (root["requestId"_L1].toString().isEmpty() ||
                (status != u"accepted" && status != u"rejected" &&
                 status != u"uncertain"))
            {
                parsed.error = u"invalid native chat result"_s;
                return parsed;
            }
        }
    }

    return parsed;
}

AttachmentIdentity parseAttachmentIdentity(const QJsonObject &root)
{
    AttachmentIdentity identity;
    const auto sessionId = root["sessionId"_L1];
    if (sessionId.isString() && sessionId.toString().size() <= 256)
    {
        identity.sessionId = sessionId.toString();
    }

    const auto browserWindowId = root["browserWindowId"_L1];
    if (browserWindowId.isString() && browserWindowId.toString().size() <= 128)
    {
        identity.browserWindowId = browserWindowId.toString();
    }
    else if (browserWindowId.isDouble() &&
             std::isfinite(browserWindowId.toDouble()) &&
             std::floor(browserWindowId.toDouble()) ==
                 browserWindowId.toDouble() &&
             browserWindowId.toDouble() >= 0 &&
             browserWindowId.toDouble() <= MAX_SAFE_JSON_INTEGER)
    {
        identity.browserWindowId =
            QString::number(static_cast<qint64>(browserWindowId.toDouble()));
    }

    const auto parseNonNegativeInteger = [](const QJsonValue &value) -> qint64 {
        if (!value.isDouble())
        {
            return -1;
        }
        const auto number = value.toDouble();
        if (!std::isfinite(number) || std::floor(number) != number ||
            number < 0 || number > MAX_SAFE_JSON_INTEGER)
        {
            return -1;
        }
        return static_cast<qint64>(number);
    };
    identity.tabId = parseNonNegativeInteger(root["tabId"_L1]);
    identity.generation = parseNonNegativeInteger(root["generation"_L1]);
    identity.channel = root["name"_L1].toString(root["channel"_L1].toString());
    if (identity.channel.size() > 128)
    {
        identity.channel.clear();
    }
    return identity;
}

}  // namespace chatterino::nm
