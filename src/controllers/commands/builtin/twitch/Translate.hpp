// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
#pragma once

namespace chatterino {
struct CommandContext;

namespace commands {

QString translate(const CommandContext &ctx);
QString translateTo(const CommandContext &ctx);
QString sayTranslate(const CommandContext &ctx);
QString nameHistory(const CommandContext &ctx);
QString logs(const CommandContext &ctx);

}  // namespace commands
}  // namespace chatterino
