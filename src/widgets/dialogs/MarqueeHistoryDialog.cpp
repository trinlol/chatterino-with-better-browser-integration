// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/MarqueeHistoryDialog.hpp"

#include "Application.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/buttons/Button.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/splits/Split.hpp"

#include <QBuffer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString getPillStyle(const chatterino::MarqueeEvent &ev)
{
    switch (ev.type)
    {
        case chatterino::MarqueeEvent::Type::Subscription:
            return QStringLiteral(
                "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #f59e0b, stop:1 #d97706); "
                "color: #000000; font-weight: 800; font-size: 10px; border-radius: 4px; padding: 2px 6px;");
        case chatterino::MarqueeEvent::Type::Bits:
            return QStringLiteral(
                "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #00d6d6, stop:1 #0284c7); "
                "color: #000000; font-weight: 800; font-size: 10px; border-radius: 4px; padding: 2px 6px;");
        case chatterino::MarqueeEvent::Type::Tip:
            return QStringLiteral(
                "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #10b981, stop:1 #059669); "
                "color: #000000; font-weight: 800; font-size: 10px; border-radius: 4px; padding: 2px 6px;");
        default:
            return QStringLiteral(
                "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #9146ff, stop:1 #772ce8); "
                "color: #ffffff; font-weight: 800; font-size: 10px; border-radius: 4px; padding: 2px 6px;");
    }
}

QString getPillLabel(const chatterino::MarqueeEvent &ev)
{
    switch (ev.type)
    {
        case chatterino::MarqueeEvent::Type::Subscription:
            return QStringLiteral("★ SUB");
        case chatterino::MarqueeEvent::Type::Bits:
            if (ev.bits > 0)
            {
                return QStringLiteral("Cheer%1").arg(ev.bits);
            }
            return QStringLiteral("◆ BITS");
        case chatterino::MarqueeEvent::Type::Tip:
            return QStringLiteral("$ TIP");
        default:
            return QStringLiteral("★ EVENT");
    }
}

QString getAmountColorStr(chatterino::MarqueeEvent::Type type, bool isLightTheme)
{
    if (isLightTheme)
    {
        switch (type)
        {
            case chatterino::MarqueeEvent::Type::Subscription:
                return QStringLiteral("#b45309");
            case chatterino::MarqueeEvent::Type::Bits:
                return QStringLiteral("#0284c7");
            case chatterino::MarqueeEvent::Type::Tip:
                return QStringLiteral("#059669");
            default:
                return QStringLiteral("#9146ff");
        }
    }
    else
    {
        switch (type)
        {
            case chatterino::MarqueeEvent::Type::Subscription:
                return QStringLiteral("#fcd34d");
            case chatterino::MarqueeEvent::Type::Bits:
                return QStringLiteral("#67e8f9");
            case chatterino::MarqueeEvent::Type::Tip:
                return QStringLiteral("#6ee7b7");
            default:
                return QStringLiteral("#bf94ff");
        }
    }
}

chatterino::EmotePtr findEmote(chatterino::TwitchChannel *channel, const QString &word)
{
    using namespace chatterino;
    EmoteNameView name{word};
    if (channel)
    {
        if (auto c = channel->cheerEmote(word))
        {
            if (c->animatedEmote)
            {
                return c->animatedEmote;
            }
            if (c->staticEmote)
            {
                return c->staticEmote;
            }
        }
        if (auto e = channel->seventvEmote(name))
        {
            return *e;
        }
        if (auto e = channel->bttvEmote(name))
        {
            return *e;
        }
        if (auto e = channel->ffzEmote(name))
        {
            return *e;
        }
        if (auto e = channel->twitchEmote(name))
        {
            return *e;
        }
    }
    if (auto e = getApp()->getSeventvEmotes()->globalEmote(name))
    {
        return *e;
    }
    if (auto e = getApp()->getBttvEmotes()->emote(name))
    {
        return *e;
    }
    if (auto e = getApp()->getFfzEmotes()->emote(name))
    {
        return *e;
    }
    return nullptr;
}

QString pixmapToDataUri(const QPixmap &pixmap, int targetHeight)
{
    QPixmap scaled = pixmap.scaledToHeight(targetHeight, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    scaled.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + bytes.toBase64();
}

QString formatMessageWithEmotes(chatterino::TwitchChannel *channel, const QString &text, float scale)
{
    const int emoteHeight = std::max(16, static_cast<int>(18 * scale));
    const auto words = text.split(QLatin1Char(' '), Qt::KeepEmptyParts);
    QString result;
    QHash<QString, QString> uriCache;

    for (int i = 0; i < words.size(); ++i)
    {
        const auto &word = words[i];
        if (word.isEmpty())
        {
            result += ' ';
            continue;
        }

        if (auto emote = findEmote(channel, word))
        {
            auto cached = uriCache.constFind(word);
            if (cached != uriCache.constEnd())
            {
                result += QStringLiteral("<img src=\"%1\" height=\"%2\" title=\"%3\" style=\"vertical-align: middle;\" /> ")
                              .arg(*cached, QString::number(emoteHeight), word.toHtmlEscaped());
                continue;
            }

            if (auto img = emote->images.getImage1())
            {
                auto pixmap = img->pixmapOrLoad();
                if (pixmap && !pixmap->isNull())
                {
                    QString uri = pixmapToDataUri(*pixmap, emoteHeight);
                    uriCache.insert(word, uri);
                    result += QStringLiteral("<img src=\"%1\" height=\"%2\" title=\"%3\" style=\"vertical-align: middle;\" /> ")
                                  .arg(uri, QString::number(emoteHeight), word.toHtmlEscaped());
                    continue;
                }
                else
                {
                    img->load();
                }
            }
        }

        result += word.toHtmlEscaped() + ' ';
    }

    return result.trimmed();
}

}  // namespace

namespace chatterino {

MarqueeHistoryDialog::MarqueeHistoryDialog(Split *split)
    : DraggablePopup(true, nullptr)
    , split_(split)
{
    if (split)
    {
        QObject::connect(split, &QObject::destroyed, this, &QWidget::deleteLater);
    }

    this->setWindowTitle(QStringLiteral("Recent Events History"));
    this->setFixedSize(480, 540);

    const bool isLight = this->theme->isLightTheme();
    this->setStyleSheet(QStringLiteral("QDialog { background-color: %1; }")
                            .arg(isLight ? "#f5f5f7" : "#18181b"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(10);

    // Title & Header section
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);

    auto *titleLabel = new QLabel(QStringLiteral("Recent Events History"), this);
    titleLabel->setFont(
        getApp()->getFonts()->getFont(FontStyle::UiMediumBold, this->scale()));
    titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 700; font-size: 15px;")
                                  .arg(isLight ? "#0e0e10" : "#efeff1"));
    titleCol->addWidget(titleLabel);

    this->subTitle_ = new QLabel(QStringLiteral("Live Events"), this);
    this->subTitle_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->subTitle_->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                       .arg(isLight ? "#666666" : "#adadb8"));
    titleCol->addWidget(this->subTitle_);

    headerLayout->addLayout(titleCol);
    headerLayout->addStretch();
    headerLayout->addWidget(this->createPinButton());
    rootLayout->addLayout(headerLayout);

    // Scroll area
    this->scrollArea_ = new QScrollArea(this);
    this->scrollArea_->setWidgetResizable(true);
    this->scrollArea_->setFrameShape(QFrame::NoFrame);
    this->scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->scrollArea_->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; } "
                       "QScrollBar:vertical { background: transparent; width: 8px; } "
                       "QScrollBar::thumb:vertical { background: %1; border-radius: 4px; }")
            .arg(isLight ? "#d5d5dc" : "#36363e"));

    this->listContainer_ = new QWidget(this->scrollArea_);
    this->listContainer_->setStyleSheet(QStringLiteral("background: transparent;"));
    this->listLayout_ = new QVBoxLayout(this->listContainer_);
    this->listLayout_->setContentsMargins(2, 4, 8, 4);
    this->listLayout_->setSpacing(8);

    this->scrollArea_->setWidget(this->listContainer_);
    rootLayout->addWidget(this->scrollArea_, 1);

    // Bottom Action Bar
    auto *bottomBar = new QHBoxLayout();
    bottomBar->setContentsMargins(0, 4, 0, 0);
    bottomBar->setSpacing(8);

    auto *clearBtn = new QPushButton(QStringLiteral("Clear History"), this);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 6px; padding: 6px 14px; font-weight: 600; font-size: 12px; } "
                       "QPushButton:hover { background-color: %4; }")
            .arg(isLight ? "#e5e5ea" : "#26262c",
                 isLight ? "#1f1f23" : "#efeff1",
                 isLight ? "#d5d5dc" : "#36363e",
                 isLight ? "#dcdcE2" : "#36363e"));
    QObject::connect(clearBtn, &QPushButton::clicked, this, [this]() {
        if (this->channel_)
        {
            this->channel_->clearMarqueeEvents();
        }
    });
    bottomBar->addWidget(clearBtn);

    bottomBar->addStretch();

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #9146ff; color: #ffffff; border: none; "
                       "border-radius: 6px; padding: 6px 18px; font-weight: 600; font-size: 12px; } "
                       "QPushButton:hover { background-color: #772ce8; }"));
    QObject::connect(closeBtn, &QPushButton::clicked, this, [this]() {
        this->close();
    });
    bottomBar->addWidget(closeBtn);

    rootLayout->addLayout(bottomBar);

    if (this->split_)
    {
        auto channel = this->split_->getChannel();
        if (auto *tc = dynamic_cast<TwitchChannel *>(channel.get()))
        {
            this->setChannel(tc);
        }
    }
}

void MarqueeHistoryDialog::setChannel(TwitchChannel *channel)
{
    this->connections_.clear();
    this->channel_ = channel;

    if (this->channel_)
    {
        this->setWindowTitle(
            QStringLiteral("Recent Events History - #%1").arg(this->channel_->getName()));
        if (this->subTitle_)
        {
            this->subTitle_->setText(
                QStringLiteral("Live Events for #%1").arg(this->channel_->getName()));
        }
        this->connections_.managedConnect(
            this->channel_->marqueeEventAdded,
            [this](const MarqueeEvent &) {
                this->populateEvents();
            });
        this->connections_.managedConnect(
            this->channel_->marqueeEventsCleared,
            [this]() {
                this->populateEvents();
            });
    }
    else if (this->subTitle_)
    {
        this->subTitle_->setText(QStringLiteral("Live Events"));
    }

    this->populateEvents();
}

void MarqueeHistoryDialog::populateEvents()
{
    // Clear existing layout
    QLayoutItem *child{};
    while ((child = this->listLayout_->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }

    const bool isLight = this->theme->isLightTheme();

    if (this->subTitle_ && this->channel_)
    {
        this->subTitle_->setText(
            QStringLiteral("Live Events for #%1").arg(this->channel_->getName()));
    }

    if (!this->channel_ || this->channel_->recentMarqueeEvents().empty())
    {
        auto *emptyCard = new QFrame(this->listContainer_);
        emptyCard->setStyleSheet(
            QStringLiteral("QFrame { background-color: %1; border: 1px dashed %2; "
                           "border-radius: 8px; padding: 28px; }")
                .arg(isLight ? "#ffffff" : "#26262c",
                     isLight ? "#d5d5dc" : "#36363e"));
        auto *emptyLayout = new QVBoxLayout(emptyCard);
        emptyLayout->setContentsMargins(16, 20, 16, 20);
        emptyLayout->setSpacing(6);

        auto *emptyIcon = new QLabel(QStringLiteral("✦"), emptyCard);
        emptyIcon->setAlignment(Qt::AlignCenter);
        emptyIcon->setStyleSheet(QStringLiteral("color: #9146ff; font-size: 24px;"));
        emptyLayout->addWidget(emptyIcon);

        auto *emptyLabel = new QLabel(
            QStringLiteral("No recent events recorded in this channel yet."),
            emptyCard);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        emptyLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 13px; font-weight: 500;")
                .arg(isLight ? "#666666" : "#adadb8"));
        emptyLayout->addWidget(emptyLabel);

        auto *emptySub = new QLabel(
            QStringLiteral("New Twitch subscriptions, bits cheers, and StreamElements tips will appear here live."),
            emptyCard);
        emptySub->setAlignment(Qt::AlignCenter);
        emptySub->setTextInteractionFlags(Qt::TextSelectableByMouse);
        emptySub->setStyleSheet(
            QStringLiteral("color: %1; font-size: 11px;")
                .arg(isLight ? "#999999" : "#71717a"));
        emptyLayout->addWidget(emptySub);

        this->listLayout_->addWidget(emptyCard);
        this->listLayout_->addStretch();
        return;
    }

    const auto &events = this->channel_->recentMarqueeEvents();
    // Display newest first
    for (auto it = events.rbegin(); it != events.rend(); ++it)
    {
        const auto &ev = *it;

        auto *card = new QFrame(this->listContainer_);
        card->setFrameShape(QFrame::StyledPanel);
        const auto cardBg = isLight ? "#ffffff" : "#26262c";
        const auto cardBorder = isLight ? "#e0e0e5" : "#36363e";
        card->setStyleSheet(
            QStringLiteral("QFrame { background-color: %1; border: 1px solid %2; "
                           "border-radius: 8px; }")
                .arg(cardBg, cardBorder));

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(6);

        // Row 1: Badge Pill, Twitch Badge / Cheermote, Username, Details, Amount, Timestamp
        auto *row1 = new QHBoxLayout();
        row1->setContentsMargins(0, 0, 0, 0);
        row1->setSpacing(8);

        // Badge Pill (★ SUB / Cheer100 / $ TIP)
        auto *pillLabel = new QLabel(getPillLabel(ev), card);
        pillLabel->setStyleSheet(getPillStyle(ev));
        pillLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row1->addWidget(pillLabel);

        // Twitch Badge or Cheered Bits Icon
        if (ev.badgeImage)
        {
            const auto pixmap = ev.badgeImage->pixmapOrLoad();
            if (pixmap && !pixmap->isNull())
            {
                auto *badgeLabel = new QLabel(card);
                const int badgeSize = static_cast<int>(18 * this->scale());
                badgeLabel->setPixmap(pixmap->scaled(badgeSize, badgeSize, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
                badgeLabel->setFixedSize(badgeSize, badgeSize);
                badgeLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
                row1->addWidget(badgeLabel);
            }
            else
            {
                ev.badgeImage->load();
            }
        }

        // Username (Selectable text + clickable link)
        auto *userLabel = new QLabel(
            QStringLiteral("<a href=\"%1\" style=\"color: %2; font-weight: 700; font-size: 13px; text-decoration: none;\">%3</a>")
                .arg(ev.username, ev.userColor.name(), ev.displayName.toHtmlEscaped()),
            card);
        userLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        userLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        QObject::connect(userLabel, &QLabel::linkActivated, this,
                         [this](const QString &username) {
                             if (this->split_)
                             {
                                 this->split_->getChannelView().showUserInfoPopup(username);
                             }
                         });
        row1->addWidget(userLabel);

        // Detail
        auto *detailLabel = new QLabel(ev.detailText, card);
        detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        detailLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px; background: transparent; border: none;")
                .arg(isLight ? "#333338" : "#efeff1"));
        row1->addWidget(detailLabel);

        // Amount (Highlighted with exact visual probe colors)
        if (!ev.amountText.isEmpty())
        {
            auto *amountLabel = new QLabel(QStringLiteral("(%1)").arg(ev.amountText), card);
            amountLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            amountLabel->setStyleSheet(
                QStringLiteral("color: %1; font-weight: 700; font-size: 12px; "
                               "background: transparent; border: none;")
                    .arg(getAmountColorStr(ev.type, isLight)));
            row1->addWidget(amountLabel);
        }

        row1->addStretch(1);

        // Timestamp (Right-aligned)
        auto *timeLabel = new QLabel(ev.timestamp.toString(QStringLiteral("hh:mm:ss")), card);
        timeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        timeLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 11px; background: transparent; border: none;")
                .arg(isLight ? "#8e8e93" : "#adadb8"));
        row1->addWidget(timeLabel);

        cardLayout->addLayout(row1);

        // Row 2: User Message Quote Box with 7TV/channel emotes
        if (!ev.userMessage.isEmpty())
        {
            auto *quoteBox = new QFrame(card);
            const auto quoteBg = isLight ? "#f0f0f4" : "#1e1e24";
            quoteBox->setStyleSheet(
                QStringLiteral("QFrame { background-color: %1; "
                               "border-left: 3px solid #9146ff; "
                               "border-top-right-radius: 6px; border-bottom-right-radius: 6px; "
                               "border-top-left-radius: 2px; border-bottom-left-radius: 2px; }")
                    .arg(quoteBg));

            auto *quoteLayout = new QVBoxLayout(quoteBox);
            quoteLayout->setContentsMargins(10, 6, 10, 6);

            const QString formattedMsg = formatMessageWithEmotes(
                this->channel_, ev.userMessage, this->scale());
            auto *msgLabel = new QLabel(QStringLiteral("“%1”").arg(formattedMsg), quoteBox);
            msgLabel->setTextFormat(Qt::RichText);
            msgLabel->setWordWrap(true);
            msgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
            msgLabel->setStyleSheet(
                QStringLiteral("color: %1; font-size: 12px; font-style: normal; "
                               "background: transparent; border: none;")
                    .arg(isLight ? "#1f1f23" : "#efeff1"));
            quoteLayout->addWidget(msgLabel);

            cardLayout->addWidget(quoteBox);
        }

        this->listLayout_->addWidget(card);
    }

    this->listLayout_->addStretch();
}

void MarqueeHistoryDialog::themeChangedEvent()
{
    const bool isLight = this->theme->isLightTheme();
    this->setStyleSheet(QStringLiteral("QDialog { background-color: %1; }")
                            .arg(isLight ? "#f5f5f7" : "#18181b"));
    this->populateEvents();
}

}  // namespace chatterino
