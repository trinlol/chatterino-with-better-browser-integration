// SPDX-FileCopyrightText: 2021 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/AttachToConsole.hpp"

#ifdef USEWINSDK
#    include <Windows.h>

#    include <cstdio>
#    include <tuple>
#endif

namespace chatterino {

void attachToConsole()
{
#ifdef USEWINSDK
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE *dummy = nullptr;
        std::ignore = freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::ignore = freopen_s(&dummy, "CONOUT$", "w", stderr);
    }
#endif
}

}  // namespace chatterino
