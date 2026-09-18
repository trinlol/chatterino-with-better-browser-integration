// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/MarqueeWidget.hpp"

#include "Application.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/splits/Split.hpp"

#include <QEasingCurve>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

void drawBadgeIcon(QPainter &painter, const chatterino::MarqueeEvent &ev,
                   const QRect &rect)
{
    if (ev.badgeImage)
    {
        const auto pixmap = ev.badgeImage->pixmapOrLoad();
        if (pixmap && !pixmap->isNull())
        {
            painter.drawPixmap(rect, *pixmap);
            return;
        }
    }

    // Fallback gradient badge icon matching marquee_visual_probe.html
    QLinearGradient grad(rect.topLeft(), rect.bottomRight());
    QString symbol;
    switch (ev.type)
    {
        case chatterino::MarqueeEvent::Type::Subscription:
            grad.setColorAt(0.0, QColor(0xf5, 0x9e, 0x0b));
            grad.setColorAt(1.0, QColor(0xd9, 0x77, 0x06));
            symbol = QStringLiteral("★");
            break;
        case chatterino::MarqueeEvent::Type::Bits:
            grad.setColorAt(0.0, QColor(0x00, 0xd6, 0xd6));
            grad.setColorAt(1.0, QColor(0x02, 0x84, 0xc7));
            symbol = QStringLiteral("◆");
            break;
        case chatterino::MarqueeEvent::Type::Tip:
            grad.setColorAt(0.0, QColor(0x10, 0xb9, 0x81));
            grad.setColorAt(1.0, QColor(0x05, 0x96, 0x69));
            symbol = QStringLiteral("$");
            break;
        default:
            grad.setColorAt(0.0, QColor(0x91, 0x46, 0xff));
            grad.setColorAt(1.0, QColor(0x77, 0x2c, 0xe8));
            symbol = QStringLiteral("★");
            break;
    }

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawRoundedRect(rect, 3, 3);

    painter.setPen(QColor(0x00, 0x00, 0x00));
    auto font = painter.font();
    font.setBold(true);
    font.setPixelSize(std::max(9, rect.height() - 5));
    painter.setFont(font);
    painter.drawText(rect, Qt::AlignCenter, symbol);
    painter.restore();
}

QColor getAmountColor(const chatterino::MarqueeEvent &ev, bool isLightTheme,
                      const QColor &defaultAccent)
{
    if (isLightTheme)
    {
        switch (ev.type)
        {
            case chatterino::MarqueeEvent::Type::Subscription:
                return QColor(0xb4, 0x53, 0x09);
            case chatterino::MarqueeEvent::Type::Bits:
                return QColor(0x02, 0x84, 0xc7);
            case chatterino::MarqueeEvent::Type::Tip:
                return QColor(0x05, 0x96, 0x69);
            default:
                return defaultAccent;
        }
    }
    else
    {
        switch (ev.type)
        {
            case chatterino::MarqueeEvent::Type::Subscription:
                return QColor(0xfc, 0xd3, 0x4d);
            case chatterino::MarqueeEvent::Type::Bits:
                return QColor(0x67, 0xe8, 0xf9);
            case chatterino::MarqueeEvent::Type::Tip:
                return QColor(0x6e, 0xe7, 0xb7);
            default:
                return defaultAccent;
        }
    }
}

}  // namespace

namespace chatterino {

MarqueeWidget::MarqueeWidget(Split *split)
    : BaseWidget(split)
    , split_(split)
{
    assert(this->split_ != nullptr);

    this->userToggledVisible_ = getSettings()->showMarquee.getValue();
    this->targetHeight_ = static_cast<int>(28 * this->scale());
    this->setFixedHeight(0);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    this->connect(&this->animationTimer_, &QTimer::timeout, this,
                  &MarqueeWidget::onTick);
    this->animationTimer_.start(16);  // ~60 FPS
    this->elapsedTimer_.start();

    this->collapseAnimation_.setDuration(250);
    this->collapseAnimation_.setEasingCurve(QEasingCurve::InOutQuad);

    QObject::connect(&this->collapseAnimation_, &QPropertyAnimation::finished,
                     this, [this]() {
                         if (!this->shouldBeVisible())
                         {
                             this->hide();
                         }
                     });

    getSettings()->marqueeStyle.connect(
        [this](int) {
            this->pixelOffset_ = 0.0f;
            this->update();
        },
        this->managedConnections_);

    getSettings()->marqueeFontSize.connect(
        [this](int) {
            this->updateHeight();
            this->update();
        },
        this->managedConnections_);

    this->managedConnections_.managedConnect(
        getApp()->getFonts()->fontChanged, [this] {
            this->updateHeight();
            this->update();
        });

    if (this->shouldBeVisible())
    {
        this->setFixedHeight(this->targetHeight_);
        this->show();
    }
    else
    {
        this->setFixedHeight(0);
        this->hide();
    }
}

int MarqueeWidget::currentHeight() const
{
    return this->height();
}

void MarqueeWidget::setCurrentHeight(int h)
{
    this->setFixedHeight(h);
}

QSize MarqueeWidget::sizeHint() const
{
    if (!this->shouldBeVisible() && this->height() == 0)
    {
        return QSize(0, 0);
    }
    return QSize(BaseWidget::sizeHint().width(), this->height());
}

QSize MarqueeWidget::minimumSizeHint() const
{
    return QSize(0, 0);
}

void MarqueeWidget::setChannel(TwitchChannel *channel)
{
    this->channelConnections_.clear();
    this->channel_ = channel;
    this->events_.clear();
    this->pixelOffset_ = 0.0f;

    if (this->channel_)
    {
        this->events_ = this->channel_->recentMarqueeEvents();
        this->channelConnections_.managedConnect(
            this->channel_->marqueeEventAdded,
            [this](const MarqueeEvent &event) {
                const bool wasHiddenOrEmpty =
                    this->events_.empty() || !this->isVisible() ||
                    this->height() == 0;
                this->events_.push_back(event);
                while (this->events_.size() > 15)
                {
                    this->events_.pop_front();
                }

                if (wasHiddenOrEmpty && this->userToggledVisible_)
                {
                    this->animateCollapse(false);
                    this->visibilityChanged.invoke();
                }

                this->update();
            });

        this->channelConnections_.managedConnect(
            this->channel_->marqueeEventsCleared,
            [this]() {
                this->events_.clear();
                this->pixelOffset_ = 0.0f;
                if (this->isVisible() || this->height() > 0)
                {
                    this->animateCollapse(true);
                    this->visibilityChanged.invoke();
                }
                this->update();
            });
    }

    if (this->shouldBeVisible())
    {
        this->setFixedHeight(this->targetHeight_);
        this->show();
    }
    else
    {
        this->setFixedHeight(0);
        this->hide();
    }

    this->update();
}

void MarqueeWidget::toggleUserMarquee()
{
    this->userToggledVisible_ = !this->userToggledVisible_;
    if (this->userToggledVisible_)
    {
        if (!this->events_.empty())
        {
            this->animateCollapse(false);
        }
    }
    else
    {
        if (this->height() > 0 || this->isVisible())
        {
            this->animateCollapse(true);
        }
    }
    this->visibilityChanged.invoke();
}

bool MarqueeWidget::isUserMarqueeVisible() const
{
    return this->userToggledVisible_;
}

bool MarqueeWidget::shouldBeVisible() const
{
    return this->userToggledVisible_ && !this->events_.empty();
}

void MarqueeWidget::animateCollapse(bool collapse)
{
    this->collapseAnimation_.stop();
    if (collapse)
    {
        this->collapseAnimation_.setStartValue(this->height());
        this->collapseAnimation_.setEndValue(0);
        this->collapseAnimation_.start();
    }
    else
    {
        this->show();
        this->collapseAnimation_.setStartValue(this->height());
        this->collapseAnimation_.setEndValue(this->targetHeight_);
        this->collapseAnimation_.start();
    }
}

void MarqueeWidget::onTick()
{
    const auto dt = this->elapsedTimer_.restart() / 1000.0f;
    if (this->isHovered_ || !this->isVisible() || this->events_.empty())
    {
        return;
    }

    if (getSettings()->marqueeStyle.getValue() == 0)
    {
        const float speed =
            static_cast<float>(getSettings()->marqueeSpeed.getValue()) *
            this->scale();
        this->pixelOffset_ += speed * dt;
        this->update();
    }
}

void MarqueeWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Background
    painter.fillRect(this->rect(),
                     this->theme->isLightTheme() ? QColor(0xf5, 0xf5, 0xf7)
                                                 : QColor(0x14, 0x14, 0x17));

    // Bottom border
    painter.setPen(this->theme->isLightTheme() ? QColor(0xe0, 0xe0, 0xe5)
                                               : QColor(0x28, 0x28, 0x30));
    painter.drawLine(0, this->height() - 1, this->width(), this->height() - 1);

    this->clickables_.clear();

    const int customFontSize = getSettings()->marqueeFontSize.getValue();
    auto font =
        getApp()->getFonts()->getFont(FontStyle::UiMedium, this->scale());
    font.setWeight(QFont::Medium);
    if (customFontSize > 0)
    {
        font.setPointSize(customFontSize);
    }
    painter.setFont(font);
    const QFontMetrics metrics(font);

    if (this->events_.empty())
    {
        painter.setPen(this->theme->messages.textColors.system);
        const QString emptyText = QStringLiteral("No recent events");
        painter.drawText(
            QRect(12 * this->scale(), 0, this->width(), this->height()),
            Qt::AlignVCenter | Qt::AlignLeft, emptyText);
        return;
    }

    const int style = getSettings()->marqueeStyle.getValue();
    const int capH = metrics.capHeight() > 0 ? metrics.capHeight() : metrics.ascent();
    const int badgeSize = capH;
    const int padding = static_cast<int>(8 * this->scale());
    const int itemSpacing = static_cast<int>(24 * this->scale());

    if (style == 0)
    {
        // Option A: Continuous Scrolling Ribbon
        // Calculate total width of all events
        struct RenderItem {
            const MarqueeEvent *event;
            int totalWidth;
            int badgeWidth;
            int userWidth;
            int amountWidth;
            QString amountStr;
        };

        std::vector<RenderItem> renderItems;
        int totalSequenceWidth = 0;

        for (const auto &ev : this->events_)
        {
            RenderItem item{};
            item.event = &ev;
            item.badgeWidth = badgeSize + padding / 2;
            item.userWidth = metrics.horizontalAdvance(ev.displayName);
            item.amountStr =
                QStringLiteral("(%1)").arg(ev.amountText.isEmpty() ? ev.detailText : ev.amountText);
            item.amountWidth = metrics.horizontalAdvance(item.amountStr);

            item.totalWidth = item.badgeWidth + item.userWidth + (padding / 2) +
                              item.amountWidth + itemSpacing;
            totalSequenceWidth += item.totalWidth;
            renderItems.push_back(item);
        }

        if (totalSequenceWidth <= 0)
        {
            return;
        }

        float startX = -std::fmod(this->pixelOffset_, static_cast<float>(totalSequenceWidth));
        while (startX > 0)
        {
            startX -= static_cast<float>(totalSequenceWidth);
        }

        float curX = startX;
        while (curX < this->width())
        {
            for (const auto &item : renderItems)
            {
                if (curX + item.totalWidth > 0 && curX < this->width())
                {
                    float drawX = curX;
                    const int textBaseline =
                        (this->height() - metrics.height()) / 2 + metrics.ascent();
                    const int badgeY = textBaseline - capH;

                    // Badge (Twitch image or fallback gradient icon)
                    const QRect badgeRect(static_cast<int>(drawX), badgeY,
                                          badgeSize, badgeSize);
                    drawBadgeIcon(painter, *item.event, badgeRect);
                    drawX += item.badgeWidth;

                    // Username
                    painter.setPen(item.event->userColor);
                    painter.drawText(static_cast<int>(drawX), textBaseline,
                                     item.event->displayName);
                    this->clickables_.push_back(
                        {QRect(static_cast<int>(drawX), 0, item.userWidth, this->height()),
                         item.event->username});
                    drawX += item.userWidth + padding / 2;

                    // Amount highlight (Visual Probe palette: (x mo), (500 bits), ($25.00), etc.)
                    painter.setPen(getAmountColor(*item.event, this->theme->isLightTheme(),
                                                  this->theme->accent));
                    painter.drawText(static_cast<int>(drawX), textBaseline,
                                     item.amountStr);
                    drawX += item.amountWidth;

                    // Bullet separator
                    painter.setPen(this->theme->isLightTheme()
                                       ? QColor(0xaa, 0xaa, 0xb0)
                                       : QColor(0x55, 0x55, 0x60));
                    painter.drawText(QRect(static_cast<int>(drawX), 0, itemSpacing, this->height()),
                                     Qt::AlignCenter, QStringLiteral("•"));
                }
                curX += static_cast<float>(item.totalWidth);
            }
        }
    }
    else
    {
        // Option C: Event Chips Stream (Latest 4 items)
        int curX = 12 * this->scale();
        const int maxChips = 4;
        const int chipHeight =
            static_cast<int>(std::max(20, customFontSize + 6) * this->scale());
        const int centerY = this->height() / 2;

        const int startIdx = std::max(0, static_cast<int>(this->events_.size()) - maxChips);
        for (int i = static_cast<int>(this->events_.size()) - 1; i >= startIdx; --i)
        {
            const auto &ev = this->events_[i];
            const int badgeWidth = badgeSize + 4;
            const int userWidth = metrics.horizontalAdvance(ev.displayName);
            const QString amtStr =
                QStringLiteral("(%1)").arg(ev.amountText.isEmpty() ? ev.detailText : ev.amountText);
            const int amtWidth = metrics.horizontalAdvance(amtStr);

            const int chipWidth = badgeWidth + userWidth + amtWidth + 16;
            if (curX + chipWidth > this->width() - 8)
            {
                break;
            }

            const QRect chipRect(curX, centerY - chipHeight / 2, chipWidth, chipHeight);

            // Pill background
            painter.setPen(this->theme->isLightTheme() ? QColor(0xd5, 0xd5, 0xdc)
                                                       : QColor(0x36, 0x36, 0x40));
            painter.setBrush(this->theme->isLightTheme() ? QColor(0xea, 0xea, 0xf0)
                                                         : QColor(0x1e, 0x1e, 0x24));
            painter.drawRoundedRect(chipRect, chipHeight / 2, chipHeight / 2);

            int drawX = curX + 8;
            drawBadgeIcon(painter, ev,
                          QRect(drawX, centerY - badgeSize / 2, badgeSize, badgeSize));
            drawX += badgeWidth;

            // User
            painter.setPen(ev.userColor);
            const QRect userRect(drawX, centerY - chipHeight / 2, userWidth, chipHeight);
            painter.drawText(userRect, Qt::AlignVCenter | Qt::AlignLeft, ev.displayName);
            this->clickables_.push_back({userRect, ev.username});
            drawX += userWidth + 4;

            // Amount
            painter.setPen(getAmountColor(ev, this->theme->isLightTheme(), this->theme->accent));
            painter.drawText(QRect(drawX, centerY - chipHeight / 2, amtWidth, chipHeight),
                             Qt::AlignVCenter | Qt::AlignLeft, amtStr);

            curX += chipWidth + 8;
        }
    }
}

void MarqueeWidget::enterEvent(QEnterEvent * /*event*/)
{
    this->isHovered_ = true;
}

void MarqueeWidget::leaveEvent(QEvent * /*event*/)
{
    this->isHovered_ = false;
}

void MarqueeWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        for (const auto &item : this->clickables_)
        {
            if (item.rect.contains(event->pos()))
            {
                this->split_->getChannelView().showUserInfoPopup(item.username);
                return;
            }
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        auto menu = std::make_unique<QMenu>(this);
        menu->addAction(QStringLiteral("Recent Events History…"), this->split_,
                        [this]() {
                            this->split_->showMarqueeHistory();
                        });
        menu->addAction(QStringLiteral("Toggle Marquee"), this,
                        [this]() {
                            this->toggleUserMarquee();
                        });

        auto *fontMenu = menu->addMenu(QStringLiteral("Text Size"));
        const int currentSize = getSettings()->marqueeFontSize.getValue();
        for (int size : {10, 11, 12, 13, 14, 16, 18})
        {
            QString label = QStringLiteral("%1 pt").arg(size);
            if (size == 12)
            {
                label += QStringLiteral(" (Default)");
            }
            auto *act = fontMenu->addAction(label, this, [size]() {
                getSettings()->marqueeFontSize.setValue(size);
            });
            act->setCheckable(true);
            act->setChecked(currentSize == size);
        }

        menu->exec(this->mapToGlobal(event->pos()));
    }
}

void MarqueeWidget::showEvent(QShowEvent * /*event*/)
{
    this->updateHeight();
}

void MarqueeWidget::hideEvent(QHideEvent * /*event*/)
{
}

void MarqueeWidget::scaleChangedEvent(float /*newScale*/)
{
    this->updateHeight();
}

void MarqueeWidget::themeChangedEvent()
{
    this->update();
}

void MarqueeWidget::updateHeight()
{
    const int fontSizePt = getSettings()->marqueeFontSize.getValue();
    const int baseHeight = std::max(28, fontSizePt + 14);
    this->targetHeight_ = static_cast<int>(baseHeight * this->scale());
    if (this->shouldBeVisible())
    {
        this->setFixedHeight(this->targetHeight_);
    }
    else
    {
        this->setFixedHeight(0);
    }
}

}  // namespace chatterino
