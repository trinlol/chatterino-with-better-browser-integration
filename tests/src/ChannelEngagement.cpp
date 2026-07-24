// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/ChannelEngagement.hpp"

#include "common/Channel.hpp"

#include <gtest/gtest.h>

using namespace chatterino;

TEST(ChannelEngagement, PollAndPredictionAreIndependent)
{
    Channel channel(QStringLiteral("example"), Channel::Type::Twitch);
    EngagementState poll{.title = QStringLiteral("Best map?"),
                         .options = {QStringLiteral("A"), QStringLiteral("B")},
                         .status = QStringLiteral("started")};
    EngagementState prediction{
        .title = QStringLiteral("Will we win?"),
        .options = {QStringLiteral("Yes"), QStringLiteral("No")},
        .status = QStringLiteral("locked")};

    channel.setEngagement(EngagementKind::Poll, poll);
    channel.setEngagement(EngagementKind::Prediction, prediction);
    channel.clearEngagement(EngagementKind::Poll);

    EXPECT_FALSE(channel.getEngagement(EngagementKind::Poll).has_value());
    ASSERT_TRUE(channel.getEngagement(EngagementKind::Prediction).has_value());
    EXPECT_EQ(channel.getEngagement(EngagementKind::Prediction)->title,
              QStringLiteral("Will we win?"));
}

TEST(ChannelEngagement, FormattingIncludesCountdownAndOutcome)
{
    const auto now = QDateTime::fromSecsSinceEpoch(1000, Qt::UTC);
    EngagementState running{
        .title = QStringLiteral("Best map?"),
        .options = {QStringLiteral("A"), QStringLiteral("B")},
        .status = QStringLiteral("started"),
        .closesAt = now.addSecs(65),
    };
    EXPECT_EQ(formatEngagement(EngagementKind::Poll, running, now),
              QStringLiteral("Poll: Best map? | A, B - 1:05 left"));

    EngagementState untimed{
        .title = QStringLiteral("Will we win?"),
        .options = {QStringLiteral("Yes"), QStringLiteral("No")},
        .status = QStringLiteral("started"),
    };
    EXPECT_EQ(formatEngagement(EngagementKind::Prediction, untimed, now),
              QStringLiteral("Prediction: Will we win? | Yes, No"));

    EngagementState ended{
        .title = QStringLiteral("Will we win?"),
        .status = QStringLiteral("ended"),
        .winner = QStringLiteral("Yes"),
    };
    EXPECT_EQ(formatEngagement(EngagementKind::Prediction, ended, now),
              QStringLiteral("Prediction ended: Will we win? | Outcome: Yes"));
}
