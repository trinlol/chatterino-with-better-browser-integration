// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/RoomModeBannerWidget.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace chatterino;

TEST(RoomModeBanner, shouldShow)
{
    auto makeModes = [](bool emoteOnly, bool submode) {
        TwitchChannel::RoomModes modes;
        modes.emoteOnly = emoteOnly;
        modes.submode = submode;
        return modes;
    };

    struct TestCase {
        TwitchChannel::RoomModes modes;
        bool selfSubscribed;
        bool selfMod;
        bool selfBroadcaster;
        bool expected;
        const char *description;
    };

    std::vector<TestCase> cases = {
        // sub-only suppress cases: submode=true, with sub=true -> false; mod=true -> false; broadcaster=true -> false
        {makeModes(false, true), true, false, false, false,
         "sub-only suppressed by sub"},
        {makeModes(false, true), false, true, false, false,
         "sub-only suppressed by mod"},
        {makeModes(false, true), false, false, true, false,
         "sub-only suppressed by broadcaster"},
        {makeModes(false, true), true, true, false, false,
         "sub-only suppressed by sub + mod"},
        {makeModes(false, true), true, false, true, false,
         "sub-only suppressed by sub + broadcaster"},
        {makeModes(false, true), false, true, true, false,
         "sub-only suppressed by mod + broadcaster"},
        {makeModes(false, true), true, true, true, false,
         "sub-only suppressed by all three"},

        // sub-only not suppressed: submode=true, sub=false, mod=false, broadcaster=false -> true
        {makeModes(false, true), false, false, false, true,
         "sub-only not suppressed"},

        // neither: emoteOnly=false, submode=false -> false
        {makeModes(false, false), false, false, false, false,
         "neither: all false"},
        {makeModes(false, false), true, false, false, false,
         "neither: sub true"},
        {makeModes(false, false), false, true, false, false,
         "neither: mod true"},
        {makeModes(false, false), false, false, true, false,
         "neither: broadcaster true"},
        {makeModes(false, false), true, true, true, false,
         "neither: all true"},

        // both: emoteOnly=true, submode=true, sub=true -> true
        {makeModes(true, true), true, false, false, true,
         "both: sub=true"},
        {makeModes(true, true), false, false, false, true,
         "both: none"},
        {makeModes(true, true), true, true, true, true,
         "both: all true"},
    };

    for (const auto &tc : cases)
    {
        EXPECT_EQ(RoomModeBannerWidget::shouldShow(tc.modes, tc.selfSubscribed,
                                                  tc.selfMod,
                                                  tc.selfBroadcaster),
                  tc.expected)
            << "Failed on test case: " << tc.description;
    }

    // emote-only always: emoteOnly=true with all combinations of (sub, mod, broadcaster) -> true
    auto emoteOnlyModes = makeModes(true, false);
    for (bool sub : {false, true})
    {
        for (bool mod : {false, true})
        {
            for (bool broadcaster : {false, true})
            {
                EXPECT_TRUE(RoomModeBannerWidget::shouldShow(
                    emoteOnlyModes, sub, mod, broadcaster))
                    << "Failed emote-only with sub=" << sub << ", mod=" << mod
                    << ", broadcaster=" << broadcaster;
            }
        }
    }
}