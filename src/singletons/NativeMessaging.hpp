// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Atomic.hpp"
#include "util/Expected.hpp"

#include <QJsonArray>
#include <QJsonObject>
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

        NativeMessagingServer &parent_;
    };

    void syncChannels(const QJsonArray &twitchChannels);
    void updateEngagement(const QJsonObject &root);
    void updatePinnedMessage(const QJsonObject &root);
    void updateRewardPending(const QJsonObject &root);
    void clearRewardPending(const QJsonObject &root);

    ReceiverThread *thread;
    static NativeMessagingServer *instance_;
    bool browserAttached_ = false;

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
