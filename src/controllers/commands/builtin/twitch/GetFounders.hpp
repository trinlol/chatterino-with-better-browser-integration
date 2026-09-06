// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne — MIT License
// Adapted for Chatterino Better Browser.
#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

namespace commands {

QString getFounders(const CommandContext &ctx);

}  // namespace commands

}  // namespace chatterino
