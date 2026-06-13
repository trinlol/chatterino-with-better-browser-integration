#include "widgets/splits/PinnedMessageWidget.hpp"
#include "singletons/Theme.hpp"
#include "common/Channel.hpp"

#include <QDesktopServices>
#include <QPainter>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QUrl>

namespace chatterino {

namespace {

QString formatPinnedAnnouncementText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    static const QRegularExpression sentenceSpace(
        QStringLiteral(R"((?<=[.!?])(?=[A-Za-z]))"));
    text.replace(sentenceSpace, QStringLiteral(" "));

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

    QStringList htmlLines;
    htmlLines.reserve(text.count(QLatin1Char('\n')) + 1);
    for (const QString &line :
         text.split(QLatin1Char('\n'), Qt::KeepEmptyParts))
    {
        htmlLines.append(linkifyPlainLine(line));
    }
    return htmlLines.join(QStringLiteral("<br>"));
}

}  // namespace

PinnedMessageWidget::PinnedMessageWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(8);

    this->textBrowser_ = new QTextBrowser(this);
    this->textBrowser_->setOpenExternalLinks(false);
    this->textBrowser_->setFrameShape(QFrame::NoFrame);
    this->textBrowser_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->textBrowser_->setFocusPolicy(Qt::NoFocus);
    this->textBrowser_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    this->textBrowser_->document()->setDocumentMargin(0);
    this->textBrowser_->setStyleSheet(
        "QTextBrowser { color: #ffffff; font-weight: 600; font-size: 12px; "
        "background: transparent; border: none; }"
        "QTextBrowser a { color: #e0c3ff; text-decoration: underline; }");

    connect(this->textBrowser_, &QTextBrowser::anchorClicked, this,
            [](const QUrl &url) {
                if (url.isValid())
                {
                    QDesktopServices::openUrl(url);
                }
            });

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

    layout->addWidget(this->textBrowser_, 1);
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

void PinnedMessageWidget::updateTextLayout()
{
    if (!this->textBrowser_)
    {
        return;
    }

    const int width = this->textBrowser_->viewport()->width();
    if (width <= 0)
    {
        return;
    }

    this->textBrowser_->document()->setTextWidth(width);
    const int height =
        int(std::ceil(this->textBrowser_->document()->size().height()));
    this->textBrowser_->setFixedHeight(std::max(1, height));
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
        this->textBrowser_->setHtml(formatPinnedAnnouncementHtml(text));
        this->updateTextLayout();
        this->show();
    }
}

void PinnedMessageWidget::resizeEvent(QResizeEvent *event)
{
    BaseWidget::resizeEvent(event);
    this->updateTextLayout();
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
