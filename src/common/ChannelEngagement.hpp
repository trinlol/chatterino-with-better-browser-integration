// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace chatterino {

enum class EngagementKind : std::uint8_t {
    Prediction,
    Poll,
};

struct EngagementState {
    QString title;
    QStringList options;
    QString status;
    QString winner;
    QDateTime closesAt;
};

QString formatEngagement(
    EngagementKind kind, const EngagementState &state,
    const QDateTime &now = QDateTime::currentDateTimeUtc());

}  // namespace chatterino
