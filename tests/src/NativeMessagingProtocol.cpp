// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/NativeMessagingProtocol.hpp"

#include <gtest/gtest.h>

using chatterino::nm::NativeAction;
using chatterino::nm::parseNativeMessage;

TEST(NativeMessagingProtocol, LegacyAndCurrentVersionsAreAccepted)
{
    EXPECT_EQ(parseNativeMessage({{"action", "prediction"}}).action,
              NativeAction::PredictionLegacy);
    EXPECT_EQ(
        parseNativeMessage({{"protocolVersion", 1}, {"action", "sync"}}).action,
        NativeAction::Sync);
}

TEST(NativeMessagingProtocol, FutureVersionIsRejected)
{
    const auto parsed =
        parseNativeMessage({{"protocolVersion", 2}, {"action", "sync"}});
    EXPECT_FALSE(parsed);
    EXPECT_FALSE(parsed.error.isEmpty());
}

TEST(NativeMessagingProtocol, FractionalVersionIsRejected)
{
    EXPECT_FALSE(
        parseNativeMessage({{"protocolVersion", 1.5}, {"action", "sync"}}));
}

TEST(NativeMessagingProtocol, CurrentEngagementShapeIsValidated)
{
    EXPECT_TRUE(parseNativeMessage({
        {"protocolVersion", 1},
        {"action", "engagement"},
        {"lifecycle", "remove"},
        {"kind", "poll"},
        {"channel", "example"},
    }));
    EXPECT_FALSE(parseNativeMessage({
        {"protocolVersion", 1},
        {"action", "engagement"},
        {"lifecycle", "upsert"},
        {"kind", "prediction"},
        {"channel", "example"},
    }));
}
