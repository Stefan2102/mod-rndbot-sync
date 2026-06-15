/*
 * This file is part of mod-rndbot-level-target.
 * Copyright (C) 2026 Yuof
 *
 * Derived from mod-player-bot-level-brackets (AGPL-3.0).
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

// Core AzerothCore headers
#include "mod-rndbot-level-target.h"
#include "ScriptMgr.h"
#include "Log.h"
#include "Configuration/Config.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "ArenaTeamMgr.h"
#include "World.h"
#include "Random.h"

// mod-playerbots headers
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"

// Standard library
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <cctype>
#include <cmath>

using namespace Acore::ChatCommands;

// ---- Configuration -----------------------------------------------------------
static bool   g_Enabled = true;
static uint32 g_CheckFrequency = 300;              // seconds; 0 = events only
static uint32 g_TargetPercent = 30;                // % of eligible bots in band
static uint32 g_TargetBand = 3;                    // +/- levels around target
static uint32 g_ProcessLimit = 5;                  // max adjustments per pass; 0 = all
static bool   g_AllowDownleveling = true;          // pull bots above the ceiling down
static bool   g_IgnoreGuildBotsWithRealPlayers = true;
static bool   g_IgnoreFriendListed = true;
static bool   g_IgnoreArenaTeamBots = true;
static bool   g_IgnoreGroupedBots = true;          // bots in any party/raid
static std::vector<std::string> g_ExcludeBotNames;
static uint32 g_GuildCacheRefreshFrequency = 600;  // seconds
static bool   g_DkProgressionEnabled = false;
static uint32 g_DkRequiredState = 13;              // PROGRESSION_TBC_TIER_5
static bool   g_FullDebug = false;
static bool   g_LiteDebug = false;

// Pulled from mod-playerbots config.
static uint8  g_RandomBotMinLevel = 1;
static uint8  g_RandomBotMaxLevel = 80;
static std::string g_RandomBotAccountPrefix = "rndbot";

static void LoadConfig()
{
    g_Enabled = sConfigMgr->GetOption<bool>("RndBotLevelTarget.Enabled", true);
    g_CheckFrequency = sConfigMgr->GetOption<uint32>("RndBotLevelTarget.CheckFrequency", 300);
    g_TargetPercent = sConfigMgr->GetOption<uint32>("RndBotLevelTarget.TargetPercent", 30);
    g_TargetBand = sConfigMgr->GetOption<uint32>("RndBotLevelTarget.TargetBand", 3);
    g_ProcessLimit = sConfigMgr->GetOption<uint32>("RndBotLevelTarget.ProcessLimit", 5);
    g_AllowDownleveling = sConfigMgr->GetOption<bool>("RndBotLevelTarget.AllowDownleveling", true);
    g_IgnoreGuildBotsWithRealPlayers = sConfigMgr->GetOption<bool>("RndBotLevelTarget.IgnoreGuildBotsWithRealPlayers", true);
    g_IgnoreFriendListed = sConfigMgr->GetOption<bool>("RndBotLevelTarget.IgnoreFriendListed", true);
    g_IgnoreArenaTeamBots = sConfigMgr->GetOption<bool>("RndBotLevelTarget.IgnoreArenaTeamBots", true);
    g_IgnoreGroupedBots = sConfigMgr->GetOption<bool>("RndBotLevelTarget.IgnoreGroupedBots", true);
    g_GuildCacheRefreshFrequency = sConfigMgr->GetOption<uint32>("RndBotLevelTarget.GuildCacheRefreshFrequency", 600);
    g_DkProgressionEnabled = sConfigMgr->GetOption<bool>("RndBotLevelTarget.DeathKnightProgression.Enabled", false);
    g_DkRequiredState = sConfigMgr->GetOption<uint32>("RndBotLevelTarget.DeathKnightProgression.RequiredState", 13);
    g_FullDebug = sConfigMgr->GetOption<bool>("RndBotLevelTarget.FullDebugMode", false);
    g_LiteDebug = sConfigMgr->GetOption<bool>("RndBotLevelTarget.LiteDebugMode", false);

    g_RandomBotMinLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMinLevel", 1));
    g_RandomBotMaxLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMaxLevel", 80));
    g_RandomBotAccountPrefix = sConfigMgr->GetOption<std::string>("AiPlayerbot.RandomBotAccountPrefix", "rndbot");

    std::string excludeNames = sConfigMgr->GetOption<std::string>("RndBotLevelTarget.ExcludeNames", "");
    g_ExcludeBotNames.clear();
    std::istringstream stream(excludeNames);
    std::string name;
    while (std::getline(stream, name, ','))
    {
        name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
        if (!name.empty())
        {
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            g_ExcludeBotNames.push_back(name);
        }
    }
}

// ---- Identity ----------------------------------------------------------------
static std::vector<uint64> g_SocialFriendsList;

static bool IsPlayerBot(Player* player)
{
    if (!player)
    {
        return false;
    }
    PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
    return botAI && botAI->IsBotAI();
}

static bool IsPlayerRandomBot(Player* player)
{
    return player && sRandomPlayerbotMgr.IsRandomBot(player);
}

// ---- Exclusions --------------------------------------------------------------
static void LoadSocialFriendList()
{
    g_SocialFriendsList.clear();
    QueryResult result = CharacterDatabase.Query("SELECT friend FROM character_social WHERE flags = 1");
    if (!result)
    {
        return;
    }
    do
    {
        g_SocialFriendsList.push_back(result->Fetch()->Get<uint32>());
    } while (result->NextRow());
}

static bool BotInFriendList(Player* bot)
{
    if (!bot)
    {
        return false;
    }
    uint64 raw = bot->GetGUID().GetRawValue();
    for (uint64 friendGuid : g_SocialFriendsList)
    {
        if (friendGuid == raw)
        {
            return true;
        }
    }
    return false;
}

static bool BotInArenaTeam(Player* bot)
{
    if (!bot)
    {
        return false;
    }
    for (uint32 slot = 0; slot < MAX_ARENA_SLOT; ++slot)
    {
        if (sArenaTeamMgr->GetArenaTeamById(bot->GetArenaTeamId(slot)))
        {
            return true;
        }
    }
    return false;
}

static bool BotNameExcluded(Player* bot)
{
    if (g_ExcludeBotNames.empty())
    {
        return false;
    }
    std::string name = bot->GetName();
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    return std::find(g_ExcludeBotNames.begin(), g_ExcludeBotNames.end(), name) != g_ExcludeBotNames.end();
}

// ---- Real-player accounts / guild cache --------------------------------------
static std::unordered_set<uint32> g_RealPlayerGuildIds;

// Builds a comma-separated list of account ids that are NOT random-bot accounts
// (username does not start with the configured prefix). Returns false if the
// prefix is empty or there are no such accounts.
static bool GetRealPlayerAccountIds(std::string& accountIdsOut)
{
    accountIdsOut.clear();
    if (g_RandomBotAccountPrefix.empty())
    {
        LOG_WARN("server.loading", "[RndBotLevelTarget] AiPlayerbot.RandomBotAccountPrefix is empty; cannot identify real accounts.");
        return false;
    }

    uint32 prefixLength = static_cast<uint32>(g_RandomBotAccountPrefix.size());
    std::string prefix = g_RandomBotAccountPrefix;
    LoginDatabase.EscapeString(prefix);

    QueryResult result = LoginDatabase.Query("SELECT id FROM account WHERE LEFT(username, {}) <> '{}'", prefixLength, prefix);
    if (!result)
    {
        return false;
    }
    do
    {
        if (!accountIdsOut.empty())
        {
            accountIdsOut += ',';
        }
        accountIdsOut += std::to_string(result->Fetch()->Get<uint32>());
    } while (result->NextRow());

    return !accountIdsOut.empty();
}

// Rebuilds the set of guild ids that have at least one real-player member
// (online or offline). Membership-based, so offline guildmates still count.
static void RefreshRealPlayerGuildCache()
{
    g_RealPlayerGuildIds.clear();

    std::string accountIds;
    if (!GetRealPlayerAccountIds(accountIds))
    {
        return;
    }

    QueryResult result = CharacterDatabase.Query(
        "SELECT DISTINCT gm.guildid FROM guild_member gm "
        "JOIN characters c ON c.guid = gm.guid WHERE c.account IN ({})",
        accountIds);
    if (!result)
    {
        return;
    }
    do
    {
        g_RealPlayerGuildIds.insert(result->Fetch()->Get<uint32>());
    } while (result->NextRow());

    if (g_FullDebug || g_LiteDebug)
    {
        LOG_INFO("server.loading", "[RndBotLevelTarget] Real-player guild cache: {} guild(s).", g_RealPlayerGuildIds.size());
    }
}

static bool BotInRealPlayerGuild(Player* bot)
{
    if (!bot)
    {
        return false;
    }
    uint32 guildId = bot->GetGuildId();
    return guildId != 0 && g_RealPlayerGuildIds.count(guildId) > 0;
}

// ---- Death Knight progression gate ------------------------------------------
// Sticky for the session: once unlocked, never re-locks.
static bool g_DkUnlocked = false;

static void EvaluateDeathKnightGate()
{
    if (!g_DkProgressionEnabled)
    {
        return; // Leave mod-playerbots' own DisableDeathKnightLogin setting alone.
    }

    if (g_DkUnlocked)
    {
        sPlayerbotAIConfig.disableDeathKnightLogin = false;
        return;
    }

    std::string accountIds;
    if (!GetRealPlayerAccountIds(accountIds))
    {
        sPlayerbotAIConfig.disableDeathKnightLogin = true;
        return;
    }

    uint32 requiredQuest = 66000 + g_DkRequiredState;
    QueryResult result = CharacterDatabase.Query(
        "SELECT 1 FROM character_queststatus_rewarded qr "
        "JOIN characters c ON c.guid = qr.guid "
        "WHERE qr.quest = {} AND c.account IN ({}) LIMIT 1",
        requiredQuest, accountIds);

    if (result)
    {
        g_DkUnlocked = true;
        sPlayerbotAIConfig.disableDeathKnightLogin = false;
        if (g_FullDebug || g_LiteDebug)
        {
            LOG_INFO("server.loading", "[RndBotLevelTarget] DK gate met (state {}); Death Knight bots unlocked.", g_DkRequiredState);
        }
    }
    else
    {
        sPlayerbotAIConfig.disableDeathKnightLogin = true;
        if (g_FullDebug)
        {
            LOG_INFO("server.loading", "[RndBotLevelTarget] DK gate not met (state {}); Death Knight bots blocked.", g_DkRequiredState);
        }
    }
}

// True if this bot must NOT be adjusted.
static bool IsBotExcluded(Player* bot)
{
    if (g_IgnoreGuildBotsWithRealPlayers && BotInRealPlayerGuild(bot))
    {
        return true;
    }
    if (g_IgnoreFriendListed && BotInFriendList(bot))
    {
        return true;
    }
    if (g_IgnoreArenaTeamBots && BotInArenaTeam(bot))
    {
        return true;
    }
    if (g_IgnoreGroupedBots && bot && bot->GetGroup())
    {
        return true;
    }
    if (BotNameExcluded(bot))
    {
        return true;
    }
    return false;
}

// ---- Adjustment pass ---------------------------------------------------------

// True if the bot's transient state allows a level change right now. Persistent
// exclusions are handled separately by IsBotExcluded. Mirrors the source
// module's IsBotSafeForLevelReset (minus the group check, which is an exclusion
// here). Caller guarantees a non-null, in-world bot.
static bool IsBotSafeToAdjust(Player* bot)
{
    if (!bot->IsAlive())
    {
        return false;
    }
    if (bot->IsInCombat())
    {
        return false;
    }
    if (bot->InBattleground() || bot->InArena() || bot->inRandomLfgDungeon() || bot->InBattlegroundQueue())
    {
        return false;
    }
    if (bot->IsInFlight())
    {
        return false;
    }
    return true;
}

// Sets a single bot's level to a random value in [lower, upper] and re-randomizes
// gear/stats. Used both to raise (promote) and lower (downlevel) a bot. Death
// Knights are floored at 55; if the range is entirely below 55 the bot is
// skipped. Returns false (no change) for bots that are logging out or not safe
// to change right now (dead, in combat, in an instance/queue, in flight).
// Adapted from mod-player-bot-level-brackets' AdjustBotToRange.
static bool SetBotLevelInRange(Player* bot, int lower, int upper)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession() ||
        bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
    {
        return false;
    }

    if (!IsBotSafeToAdjust(bot))
    {
        return false;
    }

    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        if (upper < 55)
        {
            return false;
        }
        if (lower < 55)
        {
            lower = 55;
        }
    }
    if (lower > upper)
    {
        return false;
    }

    uint8 newLevel = static_cast<uint8>(urand(lower, upper));

    if (bot->IsMounted())
    {
        bot->Dismount();
    }

    PlayerbotFactory factory(bot, newLevel);
    factory.Randomize(false);

    // Known mod-playerbots quirk: re-init talents when rolling to max level with
    // equip/spec persistence enabled.
    if (newLevel == g_RandomBotMaxLevel && sPlayerbotAIConfig.equipAndSpecPersistence)
    {
        PlayerbotFactory talentFactory(bot, newLevel);
        talentFactory.InitTalentsTree(false, true, true);
    }

    if (g_FullDebug)
    {
        LOG_INFO("server.loading", "[RndBotLevelTarget] Set bot '{}' to level {}.", bot->GetName(), newLevel);
    }
    return true;
}

static void RunAdjustmentPass()
{
    if (!g_Enabled || (g_TargetPercent == 0 && !g_AllowDownleveling))
    {
        return; // Nothing to do.
    }

    int target = -1;
    std::vector<Player*> eligible;

    auto const& players = ObjectAccessor::GetPlayers();
    for (auto const& itr : players)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
        {
            continue;
        }

        if (!IsPlayerBot(player))
        {
            // Real player: contributes to the target.
            target = std::max(target, static_cast<int>(player->GetLevel()));
            continue;
        }

        if (!IsPlayerRandomBot(player) || IsBotExcluded(player))
        {
            continue;
        }
        eligible.push_back(player);
    }

    if (target < 0)
    {
        return; // No real player online: freeze.
    }
    if (eligible.empty())
    {
        return;
    }

    // The band's upper edge is the ceiling: the highest real player's level plus
    // the band, treated as the effective maximum for the whole bot population.
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    int bandLower = target - static_cast<int>(g_TargetBand);
    if (bandLower < static_cast<int>(g_RandomBotMinLevel))
    {
        bandLower = g_RandomBotMinLevel;
    }
    int ceiling = target + static_cast<int>(g_TargetBand);
    if (ceiling > static_cast<int>(maxLevel))
    {
        ceiling = static_cast<int>(maxLevel);
    }

    bool const unlimited = (g_ProcessLimit == 0);
    uint32 budget = g_ProcessLimit;
    uint32 downleveled = 0;
    uint32 promoted = 0;

    // ---- Step 1: enforce the ceiling (downlevel bots above it) ----
    if (g_AllowDownleveling)
    {
        std::vector<Player*> aboveCeiling;
        for (Player* bot : eligible)
        {
            if (static_cast<int>(bot->GetLevel()) > ceiling)
            {
                aboveCeiling.push_back(bot);
            }
        }
        // Highest first: bring the most out-of-place bots down first.
        std::sort(aboveCeiling.begin(), aboveCeiling.end(), [](Player* a, Player* b)
        {
            return a->GetLevel() > b->GetLevel();
        });
        for (Player* bot : aboveCeiling)
        {
            if (!unlimited && budget == 0)
            {
                break;
            }
            // Scatter across [RandomBotMinLevel, ceiling]. Unsafe bots (combat,
            // instance, flight, ...) are skipped without spending budget.
            if (SetBotLevelInRange(bot, g_RandomBotMinLevel, ceiling))
            {
                ++downleveled;
                if (!unlimited)
                {
                    --budget;
                }
            }
        }
    }

    // ---- Step 2: concentrate TargetPercent% inside the band (promote up) ----
    if (g_TargetPercent > 0 && (unlimited || budget > 0))
    {
        uint32 desiredNear = static_cast<uint32>(std::lround(g_TargetPercent / 100.0 * eligible.size()));

        uint32 currentNear = 0;
        std::vector<Player*> belowBand;
        for (Player* bot : eligible)
        {
            int level = static_cast<int>(bot->GetLevel());
            if (level >= bandLower && level <= ceiling)
            {
                ++currentNear;
            }
            else if (level < bandLower)
            {
                belowBand.push_back(bot);
            }
        }

        if (currentNear < desiredNear)
        {
            uint32 need = desiredNear - currentNear;

            // Lowest-level first, so the population moves upward into the band.
            std::sort(belowBand.begin(), belowBand.end(), [](Player* a, Player* b)
            {
                return a->GetLevel() < b->GetLevel();
            });

            for (Player* bot : belowBand)
            {
                if (need == 0 || (!unlimited && budget == 0))
                {
                    break;
                }
                // Unsafe bots are skipped without spending budget or the deficit.
                if (SetBotLevelInRange(bot, bandLower, ceiling))
                {
                    ++promoted;
                    --need;
                    if (!unlimited)
                    {
                        --budget;
                    }
                }
            }
        }
    }

    if ((g_FullDebug || g_LiteDebug) && (downleveled > 0 || promoted > 0))
    {
        LOG_INFO("server.loading", "[RndBotLevelTarget] Target {} band [{}-{}], eligible {}, downleveled {}, promoted {}.",
                 target, bandLower, ceiling, eligible.size(), downleveled, promoted);
    }
}

// ---- Cache refresh helper ----------------------------------------------------
static void RefreshCaches()
{
    LoadSocialFriendList();
    RefreshRealPlayerGuildCache();
}

// ---- WorldScript -------------------------------------------------------------
class RndBotLevelTargetWorldScript : public WorldScript
{
public:
    RndBotLevelTargetWorldScript()
        : WorldScript("RndBotLevelTargetWorldScript"), m_timer(0), m_guildTimer(0) { }

    void OnStartup() override
    {
        LoadConfig();
        if (!g_Enabled)
        {
            LOG_INFO("server.loading", "[RndBotLevelTarget] Disabled via configuration.");
            return;
        }
        RefreshCaches();
        EvaluateDeathKnightGate();
        LOG_INFO("server.loading", "[RndBotLevelTarget] Loaded. CheckFrequency {}s, TargetPercent {}%, TargetBand +/-{}.",
                 g_CheckFrequency, g_TargetPercent, g_TargetBand);
    }

    void OnUpdate(uint32 diff) override
    {
        if (!g_Enabled)
        {
            return;
        }

        m_guildTimer += diff;
        if (m_guildTimer >= g_GuildCacheRefreshFrequency * 1000)
        {
            m_guildTimer = 0;
            RefreshCaches();
            if (g_DkProgressionEnabled && !g_DkUnlocked)
            {
                EvaluateDeathKnightGate();
            }
        }

        if (g_CheckFrequency == 0)
        {
            return; // Events-only mode.
        }
        m_timer += diff;
        if (m_timer >= g_CheckFrequency * 1000)
        {
            m_timer = 0;
            RunAdjustmentPass();
        }
    }

private:
    uint32 m_timer;
    uint32 m_guildTimer;
};

// ---- PlayerScript ------------------------------------------------------------
class RndBotLevelTargetPlayerScript : public PlayerScript
{
public:
    RndBotLevelTargetPlayerScript() : PlayerScript("RndBotLevelTargetPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!g_Enabled || IsPlayerBot(player))
        {
            return;
        }
        if (g_DkProgressionEnabled && !g_DkUnlocked)
        {
            EvaluateDeathKnightGate();
        }
        RunAdjustmentPass();
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!g_Enabled || IsPlayerBot(player))
        {
            return;
        }
        RunAdjustmentPass();
    }
};

// ---- CommandScript -----------------------------------------------------------
class RndBotLevelTargetCommandScript : public CommandScript
{
public:
    RndBotLevelTargetCommandScript() : CommandScript("RndBotLevelTargetCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable table =
        {
            { "rndbotlevel reload", HandleReload, SEC_ADMINISTRATOR, Console::Yes }
        };
        return table;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        LoadConfig();
        RefreshCaches();
        EvaluateDeathKnightGate();
        handler->SendSysMessage("[RndBotLevelTarget] Config reloaded.");
        return true;
    }
};

// ---- Registration ------------------------------------------------------------
void Addmod_rndbot_level_targetScripts()
{
    new RndBotLevelTargetWorldScript();
    new RndBotLevelTargetPlayerScript();
    new RndBotLevelTargetCommandScript();
}
