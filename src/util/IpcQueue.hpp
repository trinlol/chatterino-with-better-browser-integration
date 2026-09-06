// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <memory>
#include <utility>

class QByteArray;
class QString;

namespace chatterino {

class Paths;

}  // namespace chatterino

namespace chatterino::ipc {

void initPaths(const Paths *paths);

enum class DeliveryStatus {
    Delivered,
    QueueUnavailable,
    QueueFull,
    InvalidMessage,
};

[[nodiscard]] DeliveryStatus sendMessage(const char *name,
                                         const QByteArray &data);

class IpcQueuePrivate;
class IpcQueue
{
public:
    ~IpcQueue();

    static std::pair<std::unique_ptr<IpcQueue>, QString> tryReplaceOrCreate(
        const char *name, size_t maxMessages, size_t maxMessageSize);
    /// Opens a peer-owned queue without deleting an already connected peer.
    static std::pair<std::unique_ptr<IpcQueue>, QString> tryOpenOrCreate(
        const char *name, size_t maxMessages, size_t maxMessageSize);

    static bool remove(const char *name);

    // TODO: use std::expected
    /// Try to receive a message.
    /// In the case of an error, the buffer is empty.
    QByteArray receive();
    /// Wait for at most `timeout`; an empty array means timeout or error.
    QByteArray receiveFor(std::chrono::milliseconds timeout);

private:
    IpcQueue(IpcQueuePrivate *priv);

    std::unique_ptr<IpcQueuePrivate> private_;

    friend class IpcQueuePrivate;
};

}  // namespace chatterino::ipc
