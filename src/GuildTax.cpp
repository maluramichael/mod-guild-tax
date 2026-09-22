/*
 * mod-guild-tax
 *
 * A small, self-contained "guild tax" module for the mod-playerbots AzerothCore
 * fork (WotLK 3.3.5a). When a bot earns gold it pays a small percentage of that
 * income into ITS OWN guild bank, via the real Guild deposit path. Two taxable
 * events:
 *   1. Creature kill  - taxable base is derived from the victim's level.
 *   2. Quest turn-in  - taxable base is the quest's money reward.
 *
 * The tax is real gold moved out of the bot's own wallet (never seeded/cheated
 * money and never more than the bot actually holds) into the guild bank, so a
 * guild full of bots slowly fills its own bank. Real players are never taxed.
 *
 * This module was split out of mod-bot-economy so guild taxation can live and be
 * tuned on its own. See conf/mod_guild_tax.conf.dist for the keys.
 *
 * Released under GNU GPL v2; redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"

// Playerbots fork headers. "Playerbots.h" pulls in PlayerbotAI.h and
// RandomPlayerbotMgr.h (which define GET_PLAYERBOT_AI and sRandomPlayerbotMgr).
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"

#include "GuildTax.h"

#include <ctime>

namespace GuildTax
{
    Config& GetConfig()
    {
        static Config cfg;
        return cfg;
    }
}

using GuildTax::GetConfig;

namespace
{
    uint32 NowUnix()
    {
        return static_cast<uint32>(::time(nullptr));
    }

    // A taxable player is a playerbot (has bot AI). Real players never have a
    // bot AI, so they are never taxed. When OnlyRandomBots is set we further
    // narrow it to the random-bot population (RNDBOT* accounts), excluding a
    // real player's account-alt bots.
    bool IsTaxablePlayer(Player* player)
    {
        if (!player)
            return false;

        if (GET_PLAYERBOT_AI(player) == nullptr)
            return false; // a real player, never taxed

        if (GetConfig().OnlyRandomBots)
            return sRandomPlayerbotMgr.IsRandomBot(player);

        return true;
    }

    // Create our audit-log table. Deliberately NOT an SQL update file: on this
    // fork a failing module SQL aborts the whole worldserver boot, so we create
    // the table programmatically and tolerate failure at runtime instead.
    void EnsureSchema()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `guild_tax_log` ("
            "`id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT, "
            "`bot_guid` INT UNSIGNED NOT NULL, "
            "`guild_id` INT UNSIGNED NOT NULL, "
            "`gold_copper` INT UNSIGNED NOT NULL, "
            "`source` VARCHAR(16) NOT NULL, "
            "`ts` INT UNSIGNED NOT NULL, "
            "PRIMARY KEY (`id`), "
            "KEY `idx_bot` (`bot_guid`), "
            "KEY `idx_guild` (`guild_id`), "
            "KEY `idx_ts` (`ts`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
    }

    // Tax <pct>% of the gold the bot JUST EARNED this event (the taxable
    // increment - NOT the bot's whole wallet), with an optional floor so tiny
    // incomes still contribute. The tax is moved into the guild bank via the
    // REAL deposit path, Guild::HandleMemberDepositMoney(WorldSession*, uint32),
    // which atomically debits the depositing player and credits the bank inside
    // one DB transaction, so the two sides can't desync. We never tax more than
    // the bot earned this event, and never more than it actually holds.
    void PayGuildTax(Player* bot, uint32 taxableCopper, uint32 pct, char const* source)
    {
        if (taxableCopper == 0 || pct == 0)
            return;

        uint32 onHand = bot->GetMoney();
        if (onHand == 0)
            return;

        uint32 guildId = bot->GetGuildId();
        if (guildId == 0)
            return;

        Guild* guild = bot->GetGuild();
        if (!guild)
            return;

        WorldSession* session = bot->GetSession();
        if (!session)
            return;

        uint32 tax = static_cast<uint32>((static_cast<uint64>(taxableCopper) * pct) / 100);

        GuildTax::Config const& cfg = GetConfig();
        if (tax < cfg.MinTaxCopper)
            tax = cfg.MinTaxCopper;

        // Never tax more than the bot earned this event...
        if (tax > taxableCopper)
            tax = taxableCopper;

        // ...and never more than the bot actually holds (no underflow).
        if (tax > onHand)
            tax = onHand;

        if (tax == 0)
            return;

        // Real Guild deposit path: debits the bot, credits the guild bank.
        guild->HandleMemberDepositMoney(session, tax);

        uint32 botGuidLow = bot->GetGUID().GetCounter();
        CharacterDatabase.Execute(
            "INSERT INTO `guild_tax_log` (`bot_guid`, `guild_id`, `gold_copper`, `source`, `ts`) "
            "VALUES ({}, {}, {}, '{}', {})",
            botGuidLow, guildId, tax, source, NowUnix());

        GuildTax::Emit("tax_paid", {
            {"bot",    std::to_string(botGuidLow)},
            {"name",   bot->GetName()},
            {"guild",  std::to_string(guildId)},
            {"gold",   std::to_string(tax)},
            {"source", source},
        });
    }
}

// =====================================================================
//  PlayerScript: guild tax on kills and quest turn-ins.
// =====================================================================
class GuildTaxPlayerScript : public PlayerScript
{
public:
    GuildTaxPlayerScript() : PlayerScript("GuildTax_PlayerScript") { }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        GuildTax::Config const& cfg = GetConfig();
        if (!cfg.Enable || cfg.KillPct == 0)
            return;

        if (!killed || !IsTaxablePlayer(killer))
            return;

        // Base kill value derived from the victim's level (a proxy for the loot
        // gold gained from the kill). A level-1 victim yields ~1 copper of tax.
        uint32 baseValue = killed->GetLevel() * cfg.KillCopperPerLevel;
        PayGuildTax(killer, baseValue, cfg.KillPct, "kill");
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        GuildTax::Config const& cfg = GetConfig();
        if (!cfg.Enable || cfg.QuestPct == 0)
            return;

        if (!quest || !IsTaxablePlayer(player))
            return;

        // The money reward was already credited in Player::RewardQuest before
        // this hook fires. Only positive rewards are taxed (a quest that COSTS
        // money returns a negative value).
        int32 reward = quest->GetRewOrReqMoney(player->GetLevel());
        if (reward <= 0)
            return;

        PayGuildTax(player, static_cast<uint32>(reward), cfg.QuestPct, "quest");
    }
};

// =====================================================================
//  WorldScript: config load + schema.
// =====================================================================
class GuildTaxWorldScript : public WorldScript
{
public:
    GuildTaxWorldScript() : WorldScript("GuildTax_WorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        GuildTax::Config& cfg = GetConfig();
        cfg.Enable             = sConfigMgr->GetOption<bool>("GuildTax.Enable", true);
        cfg.OnlyRandomBots     = sConfigMgr->GetOption<bool>("GuildTax.OnlyRandomBots", true);
        cfg.KillPct            = sConfigMgr->GetOption<uint32>("GuildTax.KillPct", 1);
        cfg.QuestPct           = sConfigMgr->GetOption<uint32>("GuildTax.QuestPct", 1);
        cfg.KillCopperPerLevel = sConfigMgr->GetOption<uint32>("GuildTax.KillCopperPerLevel", 100);
        cfg.MinTaxCopper       = sConfigMgr->GetOption<uint32>("GuildTax.MinTaxCopper", 1);

        // Clamp percentages so a typo can't tax more than the taxable base.
        if (cfg.KillPct > 100)
            cfg.KillPct = 100;
        if (cfg.QuestPct > 100)
            cfg.QuestPct = 100;
    }

    void OnStartup() override
    {
        EnsureSchema();
    }
};

// =====================================================================
//  Registration
// =====================================================================
void AddGuildTaxScripts()
{
    new GuildTaxPlayerScript();
    new GuildTaxWorldScript();
}
