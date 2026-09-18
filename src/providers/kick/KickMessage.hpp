// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QDateTime>
#include <QString>
#include <vector>

namespace chatterino {

struct KickBadge {
    QString type;
    QString text;
    int count{0};
};

struct KickMessage {
    QString id;
    int64_t chatroomId{0};
    QString content;
    QString senderId;
    QString senderUsername;
    QString senderSlug;
    QColor senderColor;
    std::vector<KickBadge> badges;
    QDateTime createdAt;
};

}  // namespace chatterino
