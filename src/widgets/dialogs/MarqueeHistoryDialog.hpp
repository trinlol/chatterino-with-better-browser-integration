// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/MarqueeEvent.hpp"
#include "widgets/DraggablePopup.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QPointer>
#include <deque>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace chatterino {

class Split;
class TwitchChannel;

class MarqueeHistoryDialog final : public DraggablePopup
{
    Q_OBJECT

public:
    explicit MarqueeHistoryDialog(Split *split);

    void setChannel(TwitchChannel *channel);

protected:
    void themeChangedEvent() override;

private:
    void populateEvents();

    QPointer<Split> split_{};
    TwitchChannel *channel_{nullptr};

    QLabel *subTitle_{nullptr};
    QWidget *listContainer_{nullptr};
    QVBoxLayout *listLayout_{nullptr};
    QScrollArea *scrollArea_{nullptr};

    pajlada::Signals::SignalHolder connections_;
};

}  // namespace chatterino
