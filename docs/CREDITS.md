# Credits

Chatterino Better Browser builds on the work of several open-source projects.

## [Chatterino](https://chatterino.com)

The upstream project this fork is based on.
Copyright Contributors to Chatterino — MIT License.

## [Moltorino](https://codeberg.org/MoltoBenne/Moltorino)

Several features in this fork were ported from Moltorino, a Chatterino7-based
fork by MoltoBenne (MIT License):

- `/nuke` mass-moderation command with live input preview
  (`src/controllers/commands/builtin/twitch/Nuke.*`, preview wiring in
  `src/widgets/splits/SplitInput.*`)
- `/spam` and `/pyramid` fun commands (same files as `/nuke`)
- `/founders` command (`src/controllers/commands/builtin/twitch/GetFounders.*`,
  `IvrApi::getFounders`)
- `/blockterm` and `/unblockterm` commands
  (`src/controllers/commands/builtin/twitch/BlockedTerms.*` plus the
  blocked-term operations in `TwitchGql.*`)
- `/leadmod` and `/unleadmod` commands
  (`src/controllers/commands/builtin/twitch/LeadModerator.*` plus the
  channel-role mutations and user lookup in `TwitchGql.*`)
- `/editor` and `/uneditor` commands
  (`src/controllers/commands/builtin/twitch/Editor.*` plus the TV-client
  editor mutations in `TwitchGql.*`)
- `/namehistory` command and its cached lookup provider
  (`src/providers/twitch/TwitchNameHistory.*`, command in
  `src/controllers/commands/builtin/twitch/Translate.cpp`)
- `/logs` command (`src/controllers/commands/builtin/twitch/Translate.cpp`)
- `/translate`, `/translateto`, `/tl` (`/saytranslate`) commands and the
  translation provider
  (`src/providers/translation/Translator.*`, commands in
  `src/controllers/commands/builtin/twitch/Translate.cpp`)
- `/redeem` command, adapted as a chat command that lists and redeems
  channel point rewards
  (`src/controllers/commands/builtin/twitch/Redeem.*` plus the
  channel-point reward operations in `TwitchGql.*`)
- Prediction popout dialog (`src/widgets/dialogs/PredictionDialog.*`) with
  its GQL layer (`src/providers/twitch/api/TwitchGql.*`), the
  `TwitchChannel` prediction state/refresh plumbing, the channel point
  balance fetch, `formatCompactNumber`/`formatChannelPoints`
  (`src/util/Helpers.*`), and the bare `/prediction` dialog entry point
  (`src/controllers/commands/builtin/twitch/Prediction.cpp`). The dialog
  authenticates with the logged-in Chatterino account instead of
  Moltorino's separate saved-account store.

Adaptations for this fork are noted in the file headers of each ported file.
Moltorino itself is based on [Chatterino7](https://github.com/SevenTV/chatterino7)
by the SevenTV project (also MIT).

## License

This project is licensed under the MIT License — see the LICENSE file.
Ported code retains its upstream MIT licensing and attribution notices.
