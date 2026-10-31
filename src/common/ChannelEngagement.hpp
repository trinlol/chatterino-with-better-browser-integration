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
    /// Set when the user closed this activity's banner row. The activity is
    /// still tracked so later refreshes neither re-show it nor repeat its
    /// transition message.
    bool dismissed = false;
    /// Identifies one activity across its refreshes, so that delayed work
    /// (the auto-clear of an ended activity) cannot act on a newer activity
    /// that happens to share the title. 0 means unassigned.
    /// Keep this the last field: designated initializers elsewhere rely on it.
    std::uint64_t generation = 0;
};

/// Statuses the UI displays. Anything else (e.g. "canceled", "cancelled" or
/// "refunded" from older extensions) means the activity no longer exists and
/// must be treated as a removal.
[[nodiscard]] bool isDisplayableEngagementStatus(const QString &status);

/// Whether `next` is a refresh of the activity `previous` rather than a new
/// activity: the title matches and an ended activity is not restarting.
[[nodiscard]] bool isSameEngagementActivity(const EngagementState &previous,
                                            const EngagementState &next);

/// Returns a new process-unique, non-zero activity generation.
[[nodiscard]] std::uint64_t nextEngagementGeneration();

QString formatEngagement(
    EngagementKind kind, const EngagementState &state,
    const QDateTime &now = QDateTime::currentDateTimeUtc());

}  // namespace chatterino
