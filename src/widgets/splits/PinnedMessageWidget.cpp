#include "widgets/splits/PinnedMessageWidget.hpp"
#include "common/Channel.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTimer>

namespace chatterino {

namespace {

QString joinWrappedUrls(QString text)
{
    text.replace(
        QRegularExpression(QStringLiteral(R"((https?://)\s*\n\s*)")),
        QStringLiteral(R"(\1)"));
    text.replace(
        QRegularExpression(QStringLiteral(R"((www\.)\s*\n\s*)")),
        QStringLiteral(R"(\1)"));

    const QRegularExpression splitUrlContinuation(
        QStringLiteral(R"((https?://[^\s\n]*)\n([^\s\n]+))"));
    for (int i = 0; i < 8; ++i)
    {
        const QString before = text;
        text.replace(splitUrlContinuation, QStringLiteral(R"(\1\2)"));
        if (text == before)
        {
            break;
        }
    }

    return text;
}

QString applySentenceSpacingOutsideUrls(QString text)
{
    static const QRegularExpression urlRx(
        QStringLiteral(R"((https?://[^\s]+|www\.[^\s]+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sentenceSpace(
        QStringLiteral(R"((?<=[.!?])(?=[A-Za-z]))"));

    QString result;
    int last = 0;
    QRegularExpressionMatchIterator it = urlRx.globalMatch(text);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        QString before = text.mid(last, match.capturedStart() - last);
        before.replace(sentenceSpace, QStringLiteral(" "));
        result += before;
        result += match.captured();
        last = match.capturedEnd();
    }

    QString tail = text.mid(last);
    tail.replace(sentenceSpace, QStringLiteral(" "));
    result += tail;
    return result;
}

QString formatPinnedAnnouncementText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text = joinWrappedUrls(text);
    text = applySentenceSpacingOutsideUrls(text);

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

    return compacted.join(QStringLiteral(" ")).simplified();
}

QString trimTrailingUrlPunctuation(QString *url)
{
    QString trailing;
    while (!url->isEmpty())
    {
        const QChar ch = url->back();
        if (ch != QLatin1Char('.') && ch != QLatin1Char(',') &&
            ch != QLatin1Char(';') && ch != QLatin1Char('!') &&
            ch != QLatin1Char('?') && ch != QLatin1Char(')') &&
            ch != QLatin1Char(']') && ch != QLatin1Char('"') &&
            ch != QLatin1Char('\''))
        {
            break;
        }
        trailing.prepend(ch);
        url->chop(1);
    }
    return trailing;
}

QString linkifyPlainLine(const QString &line)
{
    static const QRegularExpression urlRx(
        QStringLiteral(
            R"((https?://[^\s<>"']+|www\.[^\s<>"']+))"),
        QRegularExpression::CaseInsensitiveOption);

    QString html;
    int last = 0;
    QRegularExpressionMatchIterator it = urlRx.globalMatch(line);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        html += line.mid(last, match.capturedStart() - last).toHtmlEscaped();

        QString raw = match.captured();
        QString trimmed = raw;
        const QString trailing = trimTrailingUrlPunctuation(&trimmed);

        QString href = trimmed;
        if (href.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        {
            href = QStringLiteral("https://") + href;
        }

        html += QStringLiteral(R"(<a href="%1">%2</a>)")
                    .arg(href.toHtmlEscaped(), trimmed.toHtmlEscaped());
        html += trailing.toHtmlEscaped();
        last = match.capturedEnd();
    }

    html += line.mid(last).toHtmlEscaped();
    return html;
}

QString formatPinnedAnnouncementHtml(QString text)
{
    text = formatPinnedAnnouncementText(text);
    return linkifyPlainLine(text);
}

}  // namespace

PinnedMessageWidget::PinnedMessageWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 8, 4);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignTop);

    this->textLabel_ = new QLabel(this);
    this->textLabel_->setWordWrap(true);
    this->textLabel_->setTextFormat(Qt::RichText);
    this->textLabel_->setOpenExternalLinks(true);
    this->textLabel_->setFocusPolicy(Qt::NoFocus);
    this->textLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    this->textLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    this->textLabel_->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Preferred);
    this->textLabel_->setStyleSheet(
        "QLabel { color: #ffffff; font-weight: 600; font-size: 12px; "
        "background: transparent; margin: 0; padding: 0; }"
        "a { color: #e0c3ff; text-decoration: underline; }");

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

    layout->addWidget(this->textLabel_, 1, Qt::AlignTop);
    layout->addWidget(this->closeButton_, 0, Qt::AlignTop);
    this->setLayout(layout);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

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

void PinnedMessageWidget::updateTextLayout()
{
    if (!this->textLabel_)
    {
        return;
    }

    const auto *boxLayout = qobject_cast<QHBoxLayout *>(this->layout());
    const int horizontalMargins =
        boxLayout ? boxLayout->contentsMargins().left() +
                        boxLayout->contentsMargins().right()
                  : 18;
    const int verticalMargins =
        boxLayout ? boxLayout->contentsMargins().top() +
                        boxLayout->contentsMargins().bottom()
                  : 8;
    const int spacing = boxLayout ? boxLayout->spacing() : 6;
    const int closeWidth = this->closeButton_ ? this->closeButton_->width() : 20;

    int width = this->width() - horizontalMargins - spacing - closeWidth;
    if (width <= 0)
    {
        QTimer::singleShot(0, this, [this]() {
            this->updateTextLayout();
        });
        return;
    }

    this->textLabel_->setFixedWidth(width);

    const QFontMetrics fm(this->textLabel_->font());
    const int lineHeight = fm.height();
    const QRect bounds = fm.boundingRect(
        QRect(0, 0, width, lineHeight * 12),
        Qt::AlignLeft | Qt::TextWordWrap, this->plainText_);
    this->contentHeight_ =
        qBound(lineHeight, bounds.height(), lineHeight * 8);

    this->textLabel_->setFixedHeight(this->contentHeight_);
    const int totalHeight = this->contentHeight_ + verticalMargins;
    this->setMinimumHeight(totalHeight);
    this->setMaximumHeight(totalHeight);
    this->updateGeometry();
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
        this->plainText_.clear();
        this->contentHeight_ = 0;
        this->setMinimumHeight(0);
        this->setMaximumHeight(QWIDGETSIZE_MAX);
        this->hide();
    }
    else
    {
        this->plainText_ = formatPinnedAnnouncementText(text);
        this->textLabel_->setText(
            formatPinnedAnnouncementHtml(this->plainText_));
        this->updateTextLayout();
        this->show();
    }
}

void PinnedMessageWidget::resizeEvent(QResizeEvent *event)
{
    BaseWidget::resizeEvent(event);
    this->updateTextLayout();
}

void PinnedMessageWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(this->rect(), QColor(145, 70, 255));
    painter.fillRect(QRect(0, this->height() - 1, this->width(), 1),
                     QColor(119, 44, 232));
}

void PinnedMessageWidget::themeChangedEvent()
{
    // Keeping Twitch Purple background as requested
}

}  // namespace chatterino
