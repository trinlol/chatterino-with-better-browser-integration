// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/TwitchChannel.hpp"
#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>

class QLabel;

namespace chatterino {

class RoomModeBannerWidget : public BaseWidget
{
    Q_OBJECT

public:
    explicit RoomModeBannerWidget(QWidget *parent = nullptr);

    void setChannel(TwitchChannel *channel);

    static bool shouldShow(const TwitchChannel::RoomModes &modes,
                           bool selfSubscribed, bool selfMod,
                           bool selfBroadcaster);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateState();
    QColor backgroundColor() const;
    QColor borderColor() const;

    TwitchChannel *channel_ = nullptr;
    QLabel *iconLabel_ = nullptr;
    QLabel *textLabel_ = nullptr;

    pajlada::Signals::SignalHolder channelConnections_;
};

}  // namespace chatterino