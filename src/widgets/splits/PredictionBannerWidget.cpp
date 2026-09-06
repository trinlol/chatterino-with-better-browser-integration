#include "widgets/splits/PredictionBannerWidget.hpp"

#include "common/Channel.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "widgets/dialogs/PredictionDialog.hpp"

#include <QMouseEvent>
#include <QPainter>

namespace chatterino {

PredictionBannerWidget::PredictionBannerWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    this->iconLabel_ = new QLabel(this);
    this->iconLabel_->setText(QStringLiteral("\u25cf"));
    this->iconLabel_->setStyleSheet(
        "font-size: 13px; background: transparent;");

    this->textLabel_ = new QLabel(this);
    this->textLabel_->setWordWrap(true);
    this->textLabel_->setStyleSheet(
        "color: #ffffff; font-weight: 600; font-size: 12px; "
        "background: transparent;");

    this->closeButton_ = new QPushButton(this);
    this->closeButton_->setText(QStringLiteral("\u00d7"));
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
            this->channel_->clearEngagements();
        }
    });

    this->refreshTimer_.setInterval(1000);
    connect(&this->refreshTimer_, &QTimer::timeout, this,
            &PredictionBannerWidget::updateState);

    layout->addWidget(this->iconLabel_);
    layout->addWidget(this->textLabel_, 1);
    layout->addWidget(this->closeButton_);
    this->setLayout(layout);
    this->setCursor(Qt::PointingHandCursor);
    this->hide();
}

void PredictionBannerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        this->openPredictionDialog();
    }
    BaseWidget::mouseReleaseEvent(event);
}

void PredictionBannerWidget::openPredictionDialog()
{
    // Clicking the prediction banner opens the prediction popout
    // (interaction ported from Moltorino, MIT, (c) MoltoBenne).
    auto *twitchChannel =
        dynamic_cast<TwitchChannel *>(this->channel_.get());
    if (twitchChannel == nullptr)
    {
        return;
    }
    twitchChannel->refreshPrediction(true);
    PredictionDialog::showDialog(twitchChannel, this->window());
}

void PredictionBannerWidget::setChannel(const ChannelPtr &channel)
{
    this->channel_ = channel;
    this->channelConnection_ = pajlada::Signals::ScopedConnection();

    if (channel)
    {
        this->channelConnection_ =
            channel->engagementsChanged.connect([this]() {
                this->updateState();
            });
    }
    this->updateState();
}

void PredictionBannerWidget::updateState()
{
    if (!this->channel_)
    {
        this->refreshTimer_.stop();
        this->hide();
        return;
    }

    QStringList rows;
    bool needsRefresh = false;
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto kind : {EngagementKind::Poll, EngagementKind::Prediction})
    {
        const auto &engagement = this->channel_->getEngagement(kind);
        if (!engagement)
        {
            continue;
        }
        const auto text = formatEngagement(kind, *engagement);
        if (!text.isEmpty())
        {
            rows.append(text);
        }
        needsRefresh |= engagement->status == QStringLiteral("started") &&
                        engagement->closesAt.isValid() &&
                        now < engagement->closesAt;
    }

    if (rows.isEmpty())
    {
        this->refreshTimer_.stop();
        this->hide();
        return;
    }

    this->textLabel_->setText(rows.join(QStringLiteral("\n")));
    if (needsRefresh && !this->refreshTimer_.isActive())
    {
        this->refreshTimer_.start();
    }
    else if (!needsRefresh)
    {
        this->refreshTimer_.stop();
    }
    this->show();
    this->update();
}

void PredictionBannerWidget::themeChangedEvent()
{
}

QColor PredictionBannerWidget::backgroundColor() const
{
    bool hasLocked = false;
    bool hasEnded = false;
    bool hasPrediction = false;
    if (this->channel_)
    {
        for (const auto kind :
             {EngagementKind::Poll, EngagementKind::Prediction})
        {
            const auto &engagement = this->channel_->getEngagement(kind);
            hasPrediction |=
                kind == EngagementKind::Prediction && engagement.has_value();
            hasLocked |=
                engagement && engagement->status == QStringLiteral("locked");
            hasEnded |=
                engagement && engagement->status == QStringLiteral("ended");
        }
    }

    if (hasPrediction)
    {
        return {0, 135, 90};
    }
    if (hasLocked)
    {
        return {193, 125, 17};
    }
    if (hasEnded)
    {
        return {0, 158, 96};
    }
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
    painter.fillRect(QRect(0, 0, this->width(), 1), this->borderColor());
}

}  // namespace chatterino
