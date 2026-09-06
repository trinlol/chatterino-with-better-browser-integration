// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: authenticates with the logged-in
// Chatterino account instead of Moltorino's saved-account store.
#pragma once

namespace chatterino {
struct CommandContext;

namespace commands {

QString addEditorUser(const CommandContext &ctx);
QString removeEditorUser(const CommandContext &ctx);

}  // namespace commands
}  // namespace chatterino
