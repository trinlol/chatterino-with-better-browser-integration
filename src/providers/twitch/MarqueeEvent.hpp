// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Image.hpp"

#include <QColor>
#include <QDateTime>
#include <QString>

namespace chatterino {

struct MarqueeEvent
{
    enum class Type {
        Subscription,
        Bits,
        Tip,
    };

    Type type{Type::Subscription};
    QString username;
    QString displayName;
    QColor userColor{150, 150, 150};
    QString detailText;
    QString amountText;
    int bits{0};
    ImagePtr badgeImage{nullptr};
    QDateTime timestamp{QDateTime::currentDateTime()};
    QString userMessage;
};

}  // namespace chatterino
