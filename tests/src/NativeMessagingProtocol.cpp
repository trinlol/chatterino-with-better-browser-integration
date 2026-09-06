// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/NativeMessagingProtocol.hpp"

#include <gtest/gtest.h>

using chatterino::nm::isValidNativeMessageSize;
using chatterino::nm::MAX_NATIVE_MESSAGE_SIZE;
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
        parseNativeMessage({{"protocolVersion", 3}, {"action", "sync"}});
    EXPECT_FALSE(parsed);
    EXPECT_FALSE(parsed.error.isEmpty());
}

TEST(NativeMessagingProtocol, NativeFrameSizeIsBoundedBeforeAllocation)
{
    EXPECT_FALSE(isValidNativeMessageSize(0));
    EXPECT_TRUE(isValidNativeMessageSize(MAX_NATIVE_MESSAGE_SIZE));
    EXPECT_FALSE(isValidNativeMessageSize(MAX_NATIVE_MESSAGE_SIZE + 1));
}

TEST(NativeMessagingProtocol, V2SelectRequiresCompleteIdentityAndGeometry)
{
    const QJsonObject valid{
        {"protocolVersion", 2},
        {"action", "select"},
        {"sessionId", "opaque-session"},
        {"browserWindowId", "window"},
        {"tabId", 4},
        {"generation", 2},
        {"name", "example"},
        {"browserHwnd", "1234"},
        {"leaseExpiresAt", 12345},
        {"size", QJsonObject{{"width", 320}, {"height", 480}}},
    };
    EXPECT_TRUE(parseNativeMessage(valid));

    auto invalidGeometry = valid;
    invalidGeometry["size"] = QJsonObject{{"width", 0}, {"height", 480}};
    EXPECT_FALSE(parseNativeMessage(invalidGeometry));

    auto incomplete = valid;
    incomplete.remove("sessionId");
    EXPECT_FALSE(parseNativeMessage(incomplete));

    auto coercibleIdentity = valid;
    coercibleIdentity["tabId"] = "4";
    EXPECT_FALSE(parseNativeMessage(coercibleIdentity));

    auto oversizedGeometry = valid;
    oversizedGeometry["size"] = QJsonObject{{"width", 32769}, {"height", 480}};
    EXPECT_FALSE(parseNativeMessage(oversizedGeometry));
}

TEST(NativeMessagingProtocol, V2SelectAllowsOfficialForegroundWindowFallback)
{
    const QJsonObject select{
        {"protocolVersion", 2},
        {"action", "select"},
        {"sessionId", "opaque-session"},
        {"browserWindowId", "window"},
        {"tabId", 4},
        {"generation", 2},
        {"name", "example"},
        {"leaseExpiresAt", 12345},
        {"size", QJsonObject{{"width", 320}, {"height", 480}}},
    };

    // Chromium/Edge may launch native messaging hosts with
    // --parent-window=0. Original Chatterino attaches through the foreground
    // browser window, so the v2 envelope must not reject this valid select
    // before the handler can use the same fallback.
    EXPECT_TRUE(parseNativeMessage(select));
}

TEST(NativeMessagingProtocol, ExtremeNumericVersionIsRejectedBeforeCast)
{
    EXPECT_FALSE(
        parseNativeMessage({{"protocolVersion", 1e100}, {"action", "sync"}}));
}

TEST(NativeMessagingProtocol, V2LeaseRequiresExpiry)
{
    QJsonObject lease{{"protocolVersion", 2},
                      {"action", "leaseRenew"},
                      {"sessionId", "opaque-session"},
                      {"browserWindowId", "window"},
                      {"tabId", 4},
                      {"generation", 2},
                      {"channel", "example"}};
    EXPECT_FALSE(parseNativeMessage(lease));
    lease["leaseExpiresAt"] = 12345;
    EXPECT_TRUE(parseNativeMessage(lease));
}

TEST(NativeMessagingProtocol, V2NativeChatResultIsCorrelated)
{
    QJsonObject result{{"protocolVersion", 2},
                       {"action", "nativeChatResult"},
                       {"sessionId", "opaque-session"},
                       {"browserWindowId", "window"},
                       {"tabId", 4},
                       {"generation", 2},
                       {"channel", "example"},
                       {"requestId", "request"},
                       {"status", "uncertain"}};
    EXPECT_TRUE(parseNativeMessage(result));
    result["status"] = "sent";
    EXPECT_FALSE(parseNativeMessage(result));
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
