// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"
#include "providers/combined/CombinedChannel.hpp"

#include <QObject>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace chatterino {

class CombinedManager final : public QObject
{
    Q_OBJECT

public:
    explicit CombinedManager(QObject *parent = nullptr);
    ~CombinedManager() override;

    ChannelPtr getOrAddChannel(const QString &channelName);
    ChannelPtr getChannelOrEmpty(const QString &channelName);

private:
    std::mutex mutex_;
    std::unordered_map<QString, std::shared_ptr<CombinedChannel>> channels_;
};

}  // namespace chatterino
