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

/// /blockterm <phrase>
QString blockTerm(const CommandContext &ctx);

/// /unblockterm <phrase>
QString unblockTerm(const CommandContext &ctx);

}  // namespace chatterino::commands
