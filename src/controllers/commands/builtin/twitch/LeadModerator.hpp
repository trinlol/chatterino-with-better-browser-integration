// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: authenticates with the logged-in
// Chatterino account instead of Moltorino's saved-account store.
#pragma once

class QString;

namespace chatterino {

struct CommandContext;

}  // namespace chatterino

namespace chatterino::commands {

/// /leadmod <username>
QString addLeadModerator(const CommandContext &ctx);

/// /unleadmod <username>
QString removeLeadModerator(const CommandContext &ctx);

}  // namespace chatterino::commands
