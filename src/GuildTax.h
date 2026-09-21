/*
 * mod-guild-tax - shared declarations.
 *
 * Released under GNU GPL v2; redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef MOD_GUILD_TAX_H
#define MOD_GUILD_TAX_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Weak, guarded telemetry hook. This module NEVER hard-links against the
// telemetry facility. When DAD_TELEMETRY_AVAILABLE is not defined at compile
// time the forward declaration below is absent and GuildTax::Emit() compiles
// down to a no-op. This file MUST compile cleanly with the define ABSENT.
#if defined(DAD_TELEMETRY_AVAILABLE)
namespace DadTelemetry
{
    void Emit(std::string const&, std::vector<std::pair<std::string, std::string>> const&);
}
#endif

namespace GuildTax
{
    // Cached config (populated in WorldScript::OnAfterConfigLoad).
    struct Config
    {
        bool     Enable            = true;   // module master switch
        bool     OnlyRandomBots    = true;   // tax only random bots (never real players)
        uint32   KillPct           = 1;      // % of the per-kill base value taxed on a kill
        uint32   QuestPct          = 1;      // % of the quest money reward taxed on turn-in
        uint32   KillCopperPerLevel = 100;   // per-kill taxable base = victimLevel * this (100 = 1s/level)
        uint32   MinTaxCopper      = 1;      // floor per taxed event (0 = no floor)
    };

    Config& GetConfig();

    // Thin, guarded telemetry emitter (compiles to nothing when the telemetry
    // facility is not present at build time).
    inline void Emit(std::string const& event, std::vector<std::pair<std::string, std::string>> const& fields)
    {
#if defined(DAD_TELEMETRY_AVAILABLE)
        DadTelemetry::Emit(event, fields);
#else
        (void)event;
        (void)fields;
#endif
    }
}

#endif // MOD_GUILD_TAX_H
