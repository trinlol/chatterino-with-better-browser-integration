// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/listview/GenericListItem.hpp"

#include <functional>

namespace chatterino {

class SpellCheckSuggestionItem : public GenericListItem
{
    using ActionCallback = std::function<void(const QString &)>;

public:
    SpellCheckSuggestionItem(const QString &text, ActionCallback action);

    void action() override;
    void paint(QPainter *painter, const QRect &rect) const override;
    QSize sizeHint(const QRect &rect) const override;

private:
    QString text_;
    ActionCallback action_;
};

}  // namespace chatterino
