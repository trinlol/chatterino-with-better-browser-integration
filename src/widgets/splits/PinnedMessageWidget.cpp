#include "widgets/splits/PinnedMessageWidget.hpp"
#include "singletons/Theme.hpp"
#include "common/Channel.hpp"

#include <QPainter>
#include <QRegularExpression>

namespace chatterino {

namespace {

QString formatPinnedAnnouncementText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    static const QRegularExpression sentenceSpace(
        QStringLiteral(R"((?<=[.!?])(?=[A-Za-z]))"));
    text.replace(sentenceSpace, QStringLiteral(" "));

    static const QRegularExpression clauseSpace(
        QStringLiteral(R"((?<=[,;:])(?=[A-Za-z]))"));
    text.replace(clauseSpace, QStringLiteral(" "));

    QStringList compacted;
    bool lastWasEmpty = false;
    for (QString line : text.split(QLatin1Char('\n'), Qt::KeepEmptyParts))
    {
        line = line.simplified();
        const bool empty = line.isEmpty();
        if (empty && lastWasEmpty)
        {
            continue;
        }
        compacted.append(line);
        lastWasEmpty = empty;
    }

    while (!compacted.isEmpty() && compacted.first().isEmpty())
    {
        compacted.removeFirst();
    }
    while (!compacted.isEmpty() && compacted.last().isEmpty())
    {
        compacted.removeLast();
    }

    return compacted.join(QStringLiteral("\n"));
}

}  // namespace

PinnedMessageWidget::PinnedMessageWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    this->textLabel_ = new QLabel(this);
    this->textLabel_->setWordWrap(true);
    this->textLabel_->setTextFormat(Qt::PlainText);
    this->textLabel_->setStyleSheet("color: #ffffff; font-weight: 600; font-size: 12px;");

    this->closeButton_ = new QPushButton(this);
    this->closeButton_->setText("✕");
    this->closeButton_->setFlat(true);
    this->closeButton_->setStyleSheet(
        "QPushButton { color: #ffffff; border: none; font-weight: bold; font-size: 14px; background: transparent; }"
        "QPushButton:hover { color: #f4f4f5; background: rgba(255, 255, 255, 0.2); border-radius: 4px; }"
    );
    this->closeButton_->setFixedSize(20, 20);
    this->closeButton_->setCursor(Qt::PointingHandCursor);

    connect(this->closeButton_, &QPushButton::clicked, [this]() {
        if (this->channel_)
        {
            this->channel_->dismissPinnedMessage();
        }
    });

    layout->addWidget(this->textLabel_, 1);
    layout->addWidget(this->closeButton_);
    this->setLayout(layout);

    this->hide();
}

void PinnedMessageWidget::setChannel(const ChannelPtr &channel)
{
    this->channel_ = channel;
    this->channelConnection_ = pajlada::Signals::ScopedConnection();

    if (channel)
    {
        this->channelConnection_ = channel->pinnedMessageChanged.connect([this]() {
            this->updateState();
        });
    }
    this->updateState();
}

void PinnedMessageWidget::updateState()
{
    if (!this->channel_)
    {
        this->hide();
        return;
    }

    QString text = this->channel_->getPinnedMessageText();
    if (text.isEmpty())
    {
        this->hide();
    }
    else
    {
        this->textLabel_->setText(formatPinnedAnnouncementText(text));
        this->show();
    }
}

void PinnedMessageWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw the purple background (#9146ff)
    painter.fillRect(this->rect(), QColor(145, 70, 255));
    // Draw bottom border (#772ce8)
    painter.fillRect(QRect(0, this->height() - 1, this->width(), 1), QColor(119, 44, 232));
}

void PinnedMessageWidget::themeChangedEvent()
{
    // Keeping Twitch Purple background as requested
}

}  // namespace chatterino
