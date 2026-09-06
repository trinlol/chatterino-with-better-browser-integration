// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: implemented as a chat command
// (list + redeem by title) instead of Moltorino's ChannelPointsDialog, and
// authenticates with the logged-in Chatterino account.
#pragma once

namespace chatterino {
struct CommandContext;

namespace commands {

QString redeem(const CommandContext &ctx);

}  // namespace commands
}  // namespace chatterino
