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

TEST(ChannelEngagement, UndisplayableStatusesAreRemovals)
{
    EXPECT_TRUE(isDisplayableEngagementStatus(QStringLiteral("started")));
    EXPECT_TRUE(isDisplayableEngagementStatus(QStringLiteral("locked")));
    EXPECT_TRUE(isDisplayableEngagementStatus(QStringLiteral("ended")));
    EXPECT_FALSE(isDisplayableEngagementStatus(QStringLiteral("canceled")));
    EXPECT_FALSE(isDisplayableEngagementStatus(QStringLiteral("cancelled")));
    EXPECT_FALSE(isDisplayableEngagementStatus(QStringLiteral("refunded")));
    EXPECT_FALSE(isDisplayableEngagementStatus(QString()));

    // Every status that is kept must also be shown.
    EngagementState canceled{.title = QStringLiteral("Will we win?"),
                             .status = QStringLiteral("canceled")};
    EXPECT_TRUE(
        formatEngagement(EngagementKind::Prediction, canceled).isEmpty());
}

TEST(ChannelEngagement, ActivityIdentitySurvivesRefreshesOnly)
{
    EngagementState started{.title = QStringLiteral("Best map?"),
                            .status = QStringLiteral("started")};
    EngagementState locked{.title = QStringLiteral("Best map?"),
                           .status = QStringLiteral("locked")};
    EngagementState ended{.title = QStringLiteral("Best map?"),
                          .status = QStringLiteral("ended")};
    EngagementState other{.title = QStringLiteral("Next map?"),
                          .status = QStringLiteral("started")};

    EXPECT_TRUE(isSameEngagementActivity(started, locked));
    EXPECT_TRUE(isSameEngagementActivity(locked, ended));
    EXPECT_TRUE(isSameEngagementActivity(ended, ended));
    // An ended activity followed by a started one with the same title is a
    // new activity and must not inherit the old auto-clear token.
    EXPECT_FALSE(isSameEngagementActivity(ended, started));
    EXPECT_FALSE(isSameEngagementActivity(started, other));
}

TEST(ChannelEngagement, GenerationsAreUniqueAndDefaultToUnassigned)
{
    EngagementState state{.title = QStringLiteral("Best map?")};
    EXPECT_EQ(state.generation, 0U);

    const auto first = nextEngagementGeneration();
    const auto second = nextEngagementGeneration();
    EXPECT_NE(first, 0U);
    EXPECT_GT(second, first);
}
