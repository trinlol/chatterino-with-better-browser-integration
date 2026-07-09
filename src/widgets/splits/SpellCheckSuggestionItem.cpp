// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/SpellCheckSuggestionItem.hpp"

namespace chatterino {

namespace {

constexpr int ROW_HEIGHT = 24;
constexpr int HORIZONTAL_MARGIN = 8;

}  // namespace

SpellCheckSuggestionItem::SpellCheckSuggestionItem(const QString &text,
                                                 ActionCallback action)
    : text_(text)
    , action_(std::move(action))
{
}

void SpellCheckSuggestionItem::action()
{
    if (this->action_)
    {
        this->action_(this->text_);
    }
}

void SpellCheckSuggestionItem::paint(QPainter *painter,
                                     const QRect &rect) const
{
    auto textRect = QRect(rect.topLeft() + QPoint{HORIZONTAL_MARGIN, 0},
                          QSize(rect.width() - HORIZONTAL_MARGIN * 2,
                                rect.height()));

    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, this->text_);
}

QSize SpellCheckSuggestionItem::sizeHint(const QRect &rect) const
{
    return QSize(rect.width(), ROW_HEIGHT);
}

}  // namespace chatterino
