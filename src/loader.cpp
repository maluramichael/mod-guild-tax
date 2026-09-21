/*
 * mod-guild-tax loader.
 *
 * The playerbots fork auto-globs every module's sources into one lib and looks
 * up a loader symbol derived from the folder name: for folder "mod-guild-tax"
 * that symbol is exactly "Addmod_guild_taxScripts". It must exist and call our
 * real registration function.
 *
 * Released under GNU GPL v2 or (at your option) any later version.
 */

void AddGuildTaxScripts();

void Addmod_guild_taxScripts()
{
    AddGuildTaxScripts();
}
