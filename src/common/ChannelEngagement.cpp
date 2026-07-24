// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/ChannelEngagement.hpp"

namespace chatterino {

QString formatEngagement(EngagementKind kind, const EngagementState &state,
                         const QDateTime &now)
{
    const bool isPoll = kind == EngagementKind::Poll;
    const QString eventName =
        isPoll ? QStringLiteral("Poll") : QStringLiteral("Prediction");
    QString titleAndOptions = state.title;
    if (!state.options.isEmpty())
    {
        titleAndOptions +=
            QStringLiteral(" | ") + state.options.join(QStringLiteral(", "));
    }
    const QString eventText =
        QStringLiteral("%1: %2").arg(eventName, titleAndOptions);

    if (state.status == QStringLiteral("started"))
    {
        if (!state.closesAt.isValid())
        {
            return eventText;
        }
        const auto remaining = now.secsTo(state.closesAt);
        if (remaining > 0)
        {
            const auto minutes = remaining / 60;
            const auto seconds = remaining % 60;
            return QStringLiteral("%1 - %2:%3 left")
                .arg(eventText)
                .arg(minutes)
                .arg(seconds, 2, 10, QChar('0'));
        }
        return QStringLiteral("%1 - %2").arg(
            eventText, isPoll ? QStringLiteral("closing...")
                              : QStringLiteral("locking..."));
    }
    if (state.status == QStringLiteral("locked"))
    {
        return QStringLiteral("%1: %2").arg(
            isPoll ? QStringLiteral("Poll closed")
                   : QStringLiteral("Prediction locked"),
            titleAndOptions);
    }
    if (state.status == QStringLiteral("ended"))
    {
        if (!state.winner.isEmpty())
        {
            return QStringLiteral("%1 ended: %2 | %3: %4")
                .arg(eventName, state.title,
                     isPoll ? QStringLiteral("Winning choice")
                            : QStringLiteral("Outcome"),
                     state.winner);
        }
        return QStringLiteral("%1 ended: %2").arg(eventName, state.title);
    }
    return {};
}

}  // namespace chatterino
