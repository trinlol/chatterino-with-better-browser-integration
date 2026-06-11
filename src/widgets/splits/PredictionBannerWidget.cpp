#include "widgets/splits/PredictionBannerWidget.hpp"

#include "common/Channel.hpp"

#include <QPainter>

namespace chatterino {

PredictionBannerWidget::PredictionBannerWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    this->iconLabel_ = new QLabel(this);
    this->iconLabel_->setText("🔮");
    this->iconLabel_->setStyleSheet("font-size: 13px; background: transparent;");

    this->textLabel_ = new QLabel(this);
    this->textLabel_->setWordWrap(true);
    this->textLabel_->setStyleSheet(
        "color: #ffffff; font-weight: 600; font-size: 12px; background: transparent;");

    this->closeButton_ = new QPushButton(this);
    this->closeButton_->setText("✕");
    this->closeButton_->setFlat(true);
    this->closeButton_->setStyleSheet(
        "QPushButton { color: #ffffff; border: none; font-weight: bold; "
        "font-size: 14px; background: transparent; }"
        "QPushButton:hover { color: #f4f4f5; background: rgba(255, 255, 255, "
        "0.2); border-radius: 4px; }");
    this->closeButton_->setFixedSize(20, 20);
    this->closeButton_->setCursor(Qt::PointingHandCursor);

    connect(this->closeButton_, &QPushButton::clicked, [this]() {
        if (this->channel_)
        {
            this->channel_->setPredictionState("", "");
        }
    });

    layout->addWidget(this->iconLabel_);
    layout->addWidget(this->textLabel_, 1);
    layout->addWidget(this->closeButton_);
    this->setLayout(layout);

    this->hide();
}

void PredictionBannerWidget::setChannel(const ChannelPtr &channel)
{
    this->channel_ = channel;
    this->channelConnection_ = pajlada::Signals::ScopedConnection();

    if (channel)
    {
        this->channelConnection_ = channel->predictionChanged.connect([this]() {
            this->updateState();
        });
    }
    this->updateState();
}

void PredictionBannerWidget::updateState()
{
    if (!this->channel_)
    {
        this->hide();
        return;
    }

    QString text = this->channel_->getPredictionText();
    if (text.isEmpty())
    {
        this->hide();
    }
    else
    {
        this->textLabel_->setText(text);
        this->show();
        this->update();
    }
}

QColor PredictionBannerWidget::backgroundColor() const
{
    const QString status =
        this->channel_ ? this->channel_->getPredictionStatus() : QString();

    if (status == "locked")
    {
        // Amber — submissions closed, waiting for outcome
        return {193, 125, 17};
    }
    if (status == "ended")
    {
        // Green — outcome decided
        return {0, 158, 96};
    }
    // Started / default: Twitch prediction blue
    return {26, 105, 255};
}

QColor PredictionBannerWidget::borderColor() const
{
    return this->backgroundColor().darker(130);
}

void PredictionBannerWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(this->rect(), this->backgroundColor());
    // Top border separates the banner from the chat view above it
    painter.fillRect(QRect(0, 0, this->width(), 1), this->borderColor());
}

void PredictionBannerWidget::themeChangedEvent()
{
    // Status colors are intentionally fixed regardless of theme
}

}  // namespace chatterino
