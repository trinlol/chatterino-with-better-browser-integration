// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/MarqueeEvent.hpp"
#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QTimer>

#include <deque>
#include <vector>

namespace chatterino {

class Split;
class TwitchChannel;

class MarqueeWidget final : public BaseWidget
{
    Q_OBJECT
    Q_PROPERTY(int currentHeight READ currentHeight WRITE setCurrentHeight)

public:
    explicit MarqueeWidget(Split *split);

    void setChannel(TwitchChannel *channel);
    void toggleUserMarquee();
    bool isUserMarqueeVisible() const;
    bool shouldBeVisible() const;

    int currentHeight() const;
    void setCurrentHeight(int h);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    pajlada::Signals::NoArgSignal visibilityChanged;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void scaleChangedEvent(float newScale) override;
    void themeChangedEvent() override;

private:
    void onTick();
    void updateHeight();
    void animateCollapse(bool collapse);

    struct ClickableItem {
        QRect rect;
        QString username;
    };

    Split *const split_{};
    TwitchChannel *channel_{nullptr};

    std::deque<MarqueeEvent> events_;
    std::vector<ClickableItem> clickables_;

    QTimer animationTimer_;
    QElapsedTimer elapsedTimer_;
    QPropertyAnimation collapseAnimation_{this, "currentHeight"};

    float pixelOffset_{0.0f};
    bool isHovered_{false};
    bool userToggledVisible_{true};
    int targetHeight_{28};

    pajlada::Signals::SignalHolder channelConnections_;
    pajlada::Signals::SignalHolder managedConnections_;
};

}  // namespace chatterino
