# mod-guild-tax

A small, self-contained **guild tax** module for the
[mod-playerbots](https://github.com/mod-playerbots/azerothcore-wotlk) AzerothCore
fork (WotLK 3.3.5a).

When a bot earns gold, a small percentage of that income is deposited into **its
own guild bank** via the real Guild deposit path. The tax is real gold moved out
of the bot's own wallet (never seeded/cheated money, and never more than the bot
actually holds), so a guild full of bots slowly fills its own bank.

Two taxable events:

1. **Creature kill** — the taxable base is derived from the victim's level
   (`victimLevel * GuildTax.KillCopperPerLevel`, default 1 silver/level).
2. **Quest turn-in** — the taxable base is the quest's money reward.

Real players are never taxed — only players that have a playerbot AI are
considered, and by default only random bots (`RNDBOT*` accounts).

This module was split out of `mod-bot-economy` so guild taxation can live and be
tuned on its own. It does **not** require `DadMode.Enabled`.

## How the money moves

The deposit goes through `Guild::HandleMemberDepositMoney(WorldSession*, uint32)`,
which atomically debits the depositing bot and credits the guild bank inside one
DB transaction — the module never touches bank money directly, so the two sides
can never desync.

Every taxed event is also written to a `guild_tax_log` audit table (created
programmatically at startup in the characters database) with the bot guid, guild
id, amount, source (`kill`/`quest`) and timestamp.

## Configuration

See [`conf/mod_guild_tax.conf.dist`](conf/mod_guild_tax.conf.dist). Keys:

| Key | Default | Meaning |
|-----|---------|---------|
| `GuildTax.Enable` | `1` | Module master switch |
| `GuildTax.OnlyRandomBots` | `1` | Tax only random bots (real players never taxed either way) |
| `GuildTax.KillPct` | `1` | % of the per-kill base taxed on a kill (0 disables) |
| `GuildTax.QuestPct` | `1` | % of the quest money reward taxed on turn-in (0 disables) |
| `GuildTax.KillCopperPerLevel` | `100` | Per-kill taxable base = victimLevel × this (100 = 1s/level) |
| `GuildTax.MinTaxCopper` | `1` | Floor per taxed event (0 = no floor) |

## Install

Drop the folder into the fork's `modules/` directory, re-run `cmake .` in the
build tree (a new module directory requires a CMake re-run) and rebuild
`worldserver`. The `.conf.dist` deploys like any other module config.

## License

GPL v2 (or, at your option, any later version).
