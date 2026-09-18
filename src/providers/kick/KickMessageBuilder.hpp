// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Message.hpp"
#include "providers/kick/KickEmotes.hpp"
#include "providers/kick/KickMessage.hpp"

namespace chatterino {

class KickMessageBuilder
{
public:
    static MessagePtr build(const KickMessage &msg, KickEmotes &emotes,
                            bool includePlatformBadge = true);
};

}  // namespace chatterino
