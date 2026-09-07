// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/RoomModeBannerWidget.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>

namespace chatterino {

RoomModeBannerWidget::RoomModeBannerWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    this->iconLabel_ = new QLabel(this);
    this->iconLabel_->setText(QStringLiteral("\u25cf"));
    this->iconLabel_->setStyleSheet(
        "color: #ffffff; font-size: 13px; background: transparent;");

    this->textLabel_ = new QLabel(this);
    this->textLabel_->setWordWrap(true);
    this->textLabel_->setStyleSheet(
        "color: #ffffff; font-weight: 600; font-size: 12px; "
        "background: transparent;");

    layout->addWidget(this->iconLabel_);
    layout->addWidget(this->textLabel_, 1);
    this->setLayout(layout);

    if (auto *app = getApp())
    {
        this->signalHolder_.managedConnect(
            app->getAccounts()->twitch.currentUserChanged, [this] {
                this->updateState();
            });
    }

    this->hide();
}

void RoomModeBannerWidget::setChannel(TwitchChannel *channel)
{
    this->channel_ = channel;
    this->channelConnections_.clear();

    if (this->channel_ != nullptr)
    {
        this->channelConnections_.managedConnect(
            this->channel_->roomModesChanged, [this] {
                this->updateState();
            });
        this->channelConnections_.managedConnect(
            this->channel_->userStateChanged, [this] {
                this->updateState();
            });
    }

    this->updateState();
}

bool RoomModeBannerWidget::shouldShow(const TwitchChannel::RoomModes &modes,
                                     bool selfSubscribed, bool selfMod,
                                     bool selfBroadcaster)
{
    return modes.emoteOnly ||
           (modes.submode && !(selfSubscribed || selfMod || selfBroadcaster));
}

void RoomModeBannerWidget::updateState()
{
    if (this->channel_ == nullptr)
    {
        this->hide();
        return;
    }

    auto roomModes = *this->channel_->accessRoomModes();
    bool selfSubscribed = this->channel_->isSubscribed();
    bool selfMod = this->channel_->isMod();
    bool selfBroadcaster = this->channel_->isBroadcaster();

    if (!shouldShow(roomModes, selfSubscribed, selfMod, selfBroadcaster))
    {
        this->hide();
        return;
    }

    if (roomModes.emoteOnly)
    {
        this->textLabel_->setText(
            QStringLiteral("This channel is in emote-only mode"));
    }
    else if (roomModes.submode)
    {
        this->textLabel_->setText(
            QStringLiteral("This channel is in subscriber-only mode"));
    }

    this->show();
    this->update();
}

QColor RoomModeBannerWidget::backgroundColor() const
{
    return {QStringLiteral("#772ce8")};
}

QColor RoomModeBannerWidget::borderColor() const
{
    return {QStringLiteral("#5c16c5")};
}

void RoomModeBannerWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(this->rect(), this->backgroundColor());
    painter.fillRect(QRect(0, 0, this->width(), 1), this->borderColor());
}

}  // namespace chatterino