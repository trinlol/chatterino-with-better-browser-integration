// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Atomic.hpp"
#include "util/Expected.hpp"
#include "util/IpcQueue.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QThread>

#include <optional>
#include <vector>

namespace chatterino::nm::detail {

enum class WriteManifestError : std::uint8_t {
    FailedToCreateDirectory,
    FailedToCreateFile,
};

Expected<void, WriteManifestError> writeManifestTo(QString directory,
                                                   const QString &nmDirectory,
                                                   const QString &filename,
                                                   const QJsonDocument &json);

#ifndef Q_OS_WIN
/// Parse `path` by replacing '~', '$XDG_CONFIG_HOME' and '$XDG_DATA_HOME'
/// with their respective values.
/// Returns nullopt if the path is empty or relative.
std::optional<QString> parseCustomPath(QString path);
#endif

}  // namespace chatterino::nm::detail

namespace chatterino {

class Application;
class Paths;
class Channel;
class Modes;

using ChannelPtr = std::shared_ptr<Channel>;

struct Message;
using MessagePtr = std::shared_ptr<const Message>;

void registerNmHost(const Modes &modes, const Paths &paths);
std::string &getNmQueueName(const Paths &paths);

Atomic<std::optional<QString>> &nmIpcError();

namespace nm::client {

void sendMessage(const QByteArray &array);
void writeToCout(const QByteArray &array);

}  // namespace nm::client

/// Outbound messages from Chatterino GUI to the browser extension host.
void sendToBrowserExtension(const QJsonObject &obj);

/// The desktop-side record of an acknowledged overlay.  The opaque session
/// ID, not the channel name, is the primary key because a channel may appear
/// in multiple browser tabs/windows.
struct AttachmentSession {
    QString sessionId;
    QString browserWindowId;
    qint64 tabId = -1;
    qint64 generation = -1;
    QString channel;
    quintptr browserHwnd = 0;
    qint64 leaseExpiresAt = 0;
    QString winId;
    bool ready = false;
};

class AttachmentSessionRegistry
{
public:
    enum class PrepareResult { Prepared, AlreadyCurrent, Stale, Conflict };

    PrepareResult prepare(const AttachmentSession &candidate,
                          std::optional<AttachmentSession> *replaced = nullptr);
    PrepareResult renewLease(const AttachmentSession &candidate);
    bool markReady(const QString &sessionId, qint64 generation);
    bool remove(const QString &sessionId, qint64 generation);
    std::vector<AttachmentSession> expire(qint64 now);
    std::optional<AttachmentSession> uniqueReadyForChannel(
        const QString &channel) const;
    bool containsReady(const QString &sessionId, qint64 generation) const;
    bool matchesReadyIdentity(const AttachmentSession &candidate) const;
    bool empty() const;

private:
    mutable QMutex mutex_;
    QHash<QString, AttachmentSession> sessions_;
};

class NativeMessagingServer final
{
public:
    NativeMessagingServer();
    NativeMessagingServer(const NativeMessagingServer &) = delete;
    NativeMessagingServer(NativeMessagingServer &&) = delete;
    NativeMessagingServer &operator=(const NativeMessagingServer &) = delete;
    NativeMessagingServer &operator=(NativeMessagingServer &&) = delete;
    ~NativeMessagingServer();

    void start();

    static NativeMessagingServer *instance();
    static bool isBrowserAttached();
    /// Returns a delivery result only when exactly one live browser overlay
    /// owns `channel`; ambiguous duplicate-channel sessions are never routed.
    static ipc::DeliveryStatus sendNativeChat(const QString &channel,
                                              const QString &message,
                                              const QString &requestId);

private:
    class ReceiverThread : public QThread
    {
    public:
        ReceiverThread(NativeMessagingServer &parent);

        void run() override;

    private:
        void handleMessage(const QJsonObject &root);
        void handleSelect(const QJsonObject &root);
        void handleDetach(const QJsonObject &root);
        void handleSync(const QJsonObject &root);
        void handleEngagement(const QJsonObject &root);
        void handlePinnedMessage(const QJsonObject &root);
        void handleRewardPending(const QJsonObject &root);
        void handleRewardClear(const QJsonObject &root);
        void handleLeaseRenew(const QJsonObject &root);
        void handleReconcile(const QJsonObject &root);
        void handleNativeChatResult(const QJsonObject &root);

        void expireSessions();

        NativeMessagingServer &parent_;
    };

    void syncChannels(const QJsonArray &twitchChannels);
    void updateEngagement(const QJsonObject &root);
    void updatePinnedMessage(const QJsonObject &root);
    void updateRewardPending(const QJsonObject &root);
    void clearRewardPending(const QJsonObject &root);
    void reportSession(const AttachmentSession &session, QStringView status,
                       QStringView reason = {}, QStringView requestId = {});
    void loseSession(const QString &sessionId, qint64 generation,
                     QStringView reason);

    ReceiverThread *thread;
    static NativeMessagingServer *instance_;
    AttachmentSessionRegistry sessions_;
    bool legacyBrowserAttached_ = false;

    /// This vector contains all channels that are open the user's browser.
    /// These channels are joined to be able to switch channels more quickly.
    std::vector<ChannelPtr> channelWarmer_;

    friend ReceiverThread;
};

enum class BrowserManifestFormat {
    Chrome,
    Firefox,
};

}  // namespace chatterino
