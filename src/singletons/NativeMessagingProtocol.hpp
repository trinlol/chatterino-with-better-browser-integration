// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace chatterino::nm {

inline constexpr int CURRENT_PROTOCOL_VERSION = 2;
inline constexpr std::uint32_t MAX_NATIVE_MESSAGE_SIZE = 1024U * 1024U;

[[nodiscard]] constexpr bool isValidNativeMessageSize(std::uint32_t size)
{
    return size > 0 && size <= MAX_NATIVE_MESSAGE_SIZE;
}

enum class NativeAction {
    Select,
    Detach,
    Sync,
    Engagement,
    PredictionLegacy,
    Pin,
    RewardPending,
    RewardClear,
    LeaseRenew,
    Reconcile,
    NativeChatResult,
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

/// A v2 message is intentionally additive: v0/v1 peers keep their existing
/// shapes, while a v2 peer has enough identity to reject stale cross-window
/// traffic before it reaches the UI.
struct AttachmentIdentity {
    QString sessionId;
    QString browserWindowId;
    qint64 tabId = -1;
    qint64 generation = -1;
    QString channel;

    [[nodiscard]] bool isComplete() const
    {
        return !this->sessionId.isEmpty() && !this->browserWindowId.isEmpty() &&
               this->tabId >= 0 && this->generation >= 0 &&
               !this->channel.trimmed().isEmpty();
    }
};

ParsedNativeMessage parseNativeMessage(const QJsonObject &root);
AttachmentIdentity parseAttachmentIdentity(const QJsonObject &root);

}  // namespace chatterino::nm
