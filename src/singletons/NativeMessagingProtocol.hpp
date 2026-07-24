// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QString>

namespace chatterino::nm {

inline constexpr int CURRENT_PROTOCOL_VERSION = 1;

enum class NativeAction {
    Select,
    Detach,
    Sync,
    Engagement,
    PredictionLegacy,
    Pin,
    RewardPending,
    RewardClear,
    Unknown,
};

struct ParsedNativeMessage {
    NativeAction action = NativeAction::Unknown;
    int protocolVersion = 0;
    QString error;

    explicit operator bool() const
    {
        return this->error.isEmpty() && this->action != NativeAction::Unknown;
    }
};

ParsedNativeMessage parseNativeMessage(const QJsonObject &root);

}  // namespace chatterino::nm
