// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/NativeMessagingProtocol.hpp"

#include "common/Literals.hpp"

#include <cmath>

namespace chatterino::nm {

using namespace chatterino::literals;

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
        if (std::floor(numericVersion) != numericVersion)
        {
            parsed.error = u"protocolVersion must be an integer"_s;
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

    return parsed;
}

}  // namespace chatterino::nm
