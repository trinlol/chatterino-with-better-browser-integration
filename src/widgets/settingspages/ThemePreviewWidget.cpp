// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/ThemePreviewWidget.hpp"

#include "controllers/themes/ThemeCustomizer.hpp"

#include <QPainter>
#include <QPaintEvent>

namespace chatterino {

ThemePreviewWidget::ThemePreviewWidget(ThemeCustomizer *customizer, QWidget *parent)
    : QFrame(parent)
    , customizer_(customizer)
{
    this->setObjectName("ThemePreviewWidget");
    this->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
    this->setLineWidth(1);

    if (this->customizer_)
    {
        QObject::connect(this->customizer_, &ThemeCustomizer::tokenChanged, this,
                         [this] {
                             this->update();
                         });
        QObject::connect(this->customizer_, &ThemeCustomizer::themeLoaded, this,
                         [this] {
                             this->update();
                         });
    }
}

QSize ThemePreviewWidget::sizeHint() const
{
    return QSize(420, 480);
}

QSize ThemePreviewWidget::minimumSizeHint() const
{
    return QSize(320, 360);
}

void ThemePreviewWidget::paintEvent(QPaintEvent *)
{
    if (!this->customizer_)
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const auto w = this->width();
    const auto h = this->height();

    // 1. Base Window Background
    auto windowBg = this->customizer_->color(QStringLiteral("window.background"));
    painter.fillRect(0, 0, w, h, windowBg);

    // 2. Tabs Bar (top 32px)
    const int tabH = 32;
    auto tabDivider = this->customizer_->color(QStringLiteral("tabs.dividerLine"));

    // Tab 1: Selected "#streamer"
    const int tab1W = 120;
    auto selTabBg = this->customizer_->color(QStringLiteral("tabs.selected.backgrounds.regular"));
    auto selTabLine = this->customizer_->color(QStringLiteral("tabs.selected.line.regular"));
    auto selTabText = this->customizer_->color(QStringLiteral("tabs.selected.text"));
    auto liveDot = this->customizer_->color(QStringLiteral("tabs.liveIndicator"));

    painter.fillRect(0, 0, tab1W, tabH, selTabBg);
    painter.fillRect(0, tabH - 2, tab1W, 2, selTabLine);

    // Live red dot
    painter.setPen(Qt::NoPen);
    painter.setBrush(liveDot);
    painter.drawEllipse(10, (tabH - 8) / 2, 8, 8);

    // Tab 1 text
    painter.setPen(selTabText);
    QFont tabFont = painter.font();
    tabFont.setBold(true);
    painter.setFont(tabFont);
    painter.drawText(24, 0, tab1W - 28, tabH, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("#streamer"));

    // Tab 2: Regular "#general"
    const int tab2W = 90;
    auto regTabBg = this->customizer_->color(QStringLiteral("tabs.regular.backgrounds.regular"));
    auto regTabLine = this->customizer_->color(QStringLiteral("tabs.regular.line.regular"));
    auto regTabText = this->customizer_->color(QStringLiteral("tabs.regular.text"));

    painter.fillRect(tab1W, 0, tab2W, tabH, regTabBg);
    painter.fillRect(tab1W, tabH - 1, tab2W, 1, regTabLine);
    painter.setPen(regTabText);
    tabFont.setBold(false);
    painter.setFont(tabFont);
    painter.drawText(tab1W + 10, 0, tab2W - 20, tabH, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("#general"));

    // Tab 3: Highlighted "#mentions (3)"
    const int tab3W = 110;
    auto hlTabBg = this->customizer_->color(QStringLiteral("tabs.highlighted.backgrounds.regular"));
    auto hlTabLine = this->customizer_->color(QStringLiteral("tabs.highlighted.line.regular"));
    auto hlTabText = this->customizer_->color(QStringLiteral("tabs.highlighted.text"));

    painter.fillRect(tab1W + tab2W, 0, tab3W, tabH, hlTabBg);
    painter.fillRect(tab1W + tab2W, tabH - 2, tab3W, 2, hlTabLine);
    painter.setPen(hlTabText);
    painter.drawText(tab1W + tab2W + 8, 0, tab3W - 16, tabH, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("#mentions (3)"));

    // Divider across tab bar
    painter.setPen(tabDivider);
    painter.drawLine(0, tabH, w, tabH);

    // 3. Split Header (32px to 62px)
    const int headerH = 30;
    const int headerY = tabH;
    auto headerBg = this->customizer_->color(QStringLiteral("splits.header.focusedBackground"));
    auto headerBorder = this->customizer_->color(QStringLiteral("splits.header.focusedBorder"));
    auto headerTitle = this->customizer_->color(QStringLiteral("splits.header.focusedText"));
    auto headerMeta = this->customizer_->color(QStringLiteral("splits.header.text"));

    painter.fillRect(0, headerY, w, headerH, headerBg);
    painter.setPen(headerBorder);
    painter.drawLine(0, headerY + headerH - 1, w, headerY + headerH - 1);

    QFont headerFont = painter.font();
    headerFont.setBold(true);
    painter.setFont(headerFont);
    painter.setPen(headerTitle);
    painter.drawText(12, headerY, 150, headerH, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("streamer"));

    headerFont.setBold(false);
    headerFont.setPointSize(headerFont.pointSize() - 1);
    painter.setFont(headerFont);
    painter.setPen(headerMeta);
    painter.drawText(w - 140, headerY, 130, headerH, Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("12,450 viewers"));

    // 4. Chat Area (62px to h - 48px)
    const int chatY = headerY + headerH;
    const int inputH = 44;
    const int chatH = h - chatY - inputH;

    auto splitBg = this->customizer_->color(QStringLiteral("splits.background"));
    painter.fillRect(0, chatY, w, chatH, splitBg);

    auto msgSep = this->customizer_->color(QStringLiteral("splits.messageSeperator"));
    auto msgRegularBg = this->customizer_->color(QStringLiteral("messages.backgrounds.regular"));
    auto msgAltBg = this->customizer_->color(QStringLiteral("messages.backgrounds.alternate"));
    auto msgText = this->customizer_->color(QStringLiteral("messages.textColors.regular"));
    auto linkColor = this->customizer_->color(QStringLiteral("messages.textColors.link"));
    auto systemColor = this->customizer_->color(QStringLiteral("messages.textColors.system"));
    auto hlStart = this->customizer_->color(QStringLiteral("messages.highlightAnimationStart"));
    auto accent = this->customizer_->color(QStringLiteral("accent"));
    auto disabledColor = this->customizer_->color(QStringLiteral("messages.disabled"));

    QFont chatFont = painter.font();
    chatFont.setPointSize(9);
    painter.setFont(chatFont);

    int rowY = chatY;
    auto drawBadge = [&](int x, int y, const QString &text, const QColor &badgeBg) -> int {
        int bw = 32;
        painter.setPen(Qt::NoPen);
        painter.setBrush(badgeBg);
        painter.drawRoundedRect(x, y + 5, bw, 15, 3, 3);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setPointSize(7);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(x, y + 4, bw, 16, Qt::AlignCenter, text);
        painter.setFont(chatFont);
        return x + bw + 4;
    };

    // Row 1: Regular message
    const int r1H = 28;
    painter.fillRect(0, rowY, w - 10, r1H, msgRegularBg);
    painter.setPen(systemColor);
    painter.drawText(8, rowY, 40, r1H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("12:34"));
    int curX = 48;
    curX = drawBadge(curX, rowY, QStringLiteral("MOD"), QColor("#34a853"));
    curX = drawBadge(curX, rowY, QStringLiteral("SUB"), accent);
    painter.setPen(accent);
    chatFont.setBold(true);
    painter.setFont(chatFont);
    painter.drawText(curX, rowY, 80, r1H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("ChatterOne:"));
    curX += 75;
    chatFont.setBold(false);
    painter.setFont(chatFont);
    painter.setPen(msgText);
    painter.drawText(curX, rowY, 110, r1H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Hello world! Check "));
    curX += 105;
    painter.setPen(linkColor);
    painter.drawText(curX, rowY, 100, r1H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("twitch.tv"));

    painter.setPen(msgSep);
    painter.drawLine(0, rowY + r1H - 1, w - 10, rowY + r1H - 1);
    rowY += r1H;

    // Row 2: Alternating background message
    const int r2H = 28;
    painter.fillRect(0, rowY, w - 10, r2H, msgAltBg);
    painter.setPen(systemColor);
    painter.drawText(8, rowY, 40, r2H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("12:35"));
    curX = 48;
    curX = drawBadge(curX, rowY, QStringLiteral("VIP"), QColor("#ab47bc"));
    painter.setPen(QColor("#f06292"));
    chatFont.setBold(true);
    painter.setFont(chatFont);
    painter.drawText(curX, rowY, 80, r2H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("CoolViewer:"));
    curX += 80;
    chatFont.setBold(false);
    painter.setFont(chatFont);
    painter.setPen(msgText);
    painter.drawText(curX, rowY, w - curX - 15, r2H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Live theme preview looking clean!"));

    painter.setPen(msgSep);
    painter.drawLine(0, rowY + r2H - 1, w - 10, rowY + r2H - 1);
    rowY += r2H;

    // Row 3: Highlighted mention message
    const int r3H = 30;
    painter.fillRect(0, rowY, w - 10, r3H, hlStart);
    painter.fillRect(0, rowY, 3, r3H, accent);
    painter.setPen(systemColor);
    painter.drawText(8, rowY, 40, r3H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("12:35"));
    curX = 48;
    curX = drawBadge(curX, rowY, QStringLiteral("SUB"), accent);
    painter.setPen(accent);
    chatFont.setBold(true);
    painter.setFont(chatFont);
    painter.drawText(curX, rowY, 80, r3H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("StreamFan:"));
    curX += 75;
    chatFont.setBold(false);
    painter.setFont(chatFont);
    painter.setPen(msgText);
    painter.drawText(curX, rowY, w - curX - 15, r3H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("@you Check out this color palette!"));

    painter.setPen(msgSep);
    painter.drawLine(0, rowY + r3H - 1, w - 10, rowY + r3H - 1);
    rowY += r3H;

    // Row 4: System message
    const int r4H = 26;
    painter.setPen(systemColor);
    painter.drawText(8, rowY, 40, r4H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("12:36"));
    QFont italicFont = chatFont;
    italicFont.setItalic(true);
    painter.setFont(italicFont);
    painter.drawText(48, rowY, w - 60, r4H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("-> Streamer is now live playing Science & Technology"));
    painter.setFont(chatFont);

    painter.setPen(msgSep);
    painter.drawLine(0, rowY + r4H - 1, w - 10, rowY + r4H - 1);
    rowY += r4H;

    // Row 5: Disabled / deleted message
    const int r5H = 26;
    painter.setPen(systemColor);
    painter.drawText(8, rowY, 40, r5H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("12:37"));
    painter.setPen(disabledColor);
    painter.drawText(48, rowY, w - 60, r5H, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("<message deleted by moderator>"));

    painter.setPen(msgSep);
    painter.drawLine(0, rowY + r5H - 1, w - 10, rowY + r5H - 1);

    // 5. Scrollbar (right 8px)
    auto scrollBg = this->customizer_->color(QStringLiteral("scrollbars.background"));
    auto scrollThumb = this->customizer_->color(QStringLiteral("scrollbars.thumb"));
    painter.fillRect(w - 8, chatY, 8, chatH, scrollBg);
    painter.setPen(Qt::NoPen);
    painter.setBrush(scrollThumb);
    painter.drawRoundedRect(w - 7, chatY + 20, 6, 60, 3, 3);

    // 6. Input Box (at bottom)
    const int inputY = h - inputH;
    auto inputBg = this->customizer_->color(QStringLiteral("splits.input.background"));
    auto inputBorder = this->customizer_->color(QStringLiteral("splits.input.backgroundPulse"));
    auto inputText = this->customizer_->color(QStringLiteral("splits.input.text"));
    auto placeholder = this->customizer_->color(QStringLiteral("messages.textColors.chatPlaceholder"));
    auto caretColor = this->customizer_->color(QStringLiteral("messages.textColors.caret"));

    painter.setPen(inputBorder);
    painter.setBrush(inputBg);
    painter.drawRoundedRect(8, inputY + 4, w - 16, inputH - 8, 4, 4);

    painter.setPen(placeholder);
    painter.drawText(16, inputY + 4, w - 50, inputH - 8, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Send a message..."));

    // Caret
    painter.setPen(caretColor);
    painter.drawLine(140, inputY + 12, 140, inputY + inputH - 16);

    // Emote smile hint
    painter.setPen(placeholder);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(w - 32, inputY + 13, 16, 16);
    painter.drawArc(w - 29, inputY + 17, 10, 8, 200 * 16, 140 * 16);
}

}  // namespace chatterino
