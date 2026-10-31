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

/// Per-message capacity of the IPC queues between the native host and the GUI.
/// Browser frames may be up to MAX_NATIVE_MESSAGE_SIZE, but real traffic
/// (selects, long poll/prediction titles with options) is a few KiB at most.
/// A queue reserves maxMessages * maxMessageSize bytes up front, so keep both
/// bounded: 32 * 64 KiB = 2 MiB per queue.
inline constexpr std::uint32_t MAX_IPC_MESSAGE_SIZE = 64U * 1024U;
inline constexpr std::uint32_t IPC_QUEUE_MAX_MESSAGES = 32;
static_assert(MAX_IPC_MESSAGE_SIZE <= MAX_NATIVE_MESSAGE_SIZE);

/// Whether a frame fits into the host -> GUI IPC queue.
[[nodiscard]] constexpr bool fitsIpcQueue(std::uint64_t size)
{
    return size > 0 && size <= MAX_IPC_MESSAGE_SIZE;
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
    MinuteWatchedResult,
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

/// Twitch polls and predictions run for at most 30 minutes; anything beyond a
/// day is treated as malformed browser input.
inline constexpr qint64 MAX_ENGAGEMENT_WINDOW_MS = 24LL * 60 * 60 * 1000;
inline constexpr int MAX_ENGAGEMENT_DURATION_SECONDS = 24 * 60 * 60;
inline constexpr qsizetype MAX_ENGAGEMENT_OPTIONS = 10;

/// Browser messages are untrusted. Only a strict Twitch login may reach
/// getOrAddChannel, which would otherwise create a channel for any string or
/// resolve special channels such as "/whispers" or "/mentions".
[[nodiscard]] bool isValidBrowserChannelName(const QString &name);

/// Parses an engagement `closesAt` (epoch milliseconds, number or string).
/// Returns 0 for missing, non-finite, non-positive or implausibly distant
/// values so that the result can always be cast and used safely.
[[nodiscard]] qint64 parseEngagementClosesAt(const QJsonValue &value,
                                             qint64 nowMs);

}  // namespace chatterino::nm
