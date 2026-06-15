# mod-rndbot-level-target

An [AzerothCore](https://www.azerothcore.org/) module that keeps
[mod-playerbots](https://github.com/liyunfan1223/mod-playerbots) random bots at
*your* level, so the world feels alive wherever you are — not just at level 80.

## The idea

Out of the box, mod-playerbots scatters its random bots across the whole level
range (1–80). That looks great on a populated endgame realm, but on a small or
solo server it means the handful of bots near *your* level is tiny. Level a fresh
character and you quest through a near-empty world while hundreds of bots idle at
max level somewhere you can't see them.

This module fixes that with one simple idea:

> **The highest real player online sets the level the bots gravitate to.**

Whatever level you're playing, a configurable share of the bots is kept right
around you, and the bot population never towers far above you. Log a level-18
character in and the world fills with level-15–21 bots to quest, group, and
fight alongside. Ding to 19 and they follow you up. There's always a crowd at
your level, because the crowd *is* defined by your level.

When nobody real is online, the module does nothing — it never churns bots in an
empty world.

## How it works

There is a single **target**: the level of the highest real (non-bot) player
currently online. Around it sits a **band** of `±TargetBand` levels (default 3),
and the top of that band — the target plus the band — is treated as a **ceiling**
on the entire bot population.

Each adjustment does two things, both touching only random bots:

1. **Pull the stragglers up.** Enough of the lowest-level bots are raised into the
   band so that about `TargetPercent`% of bots (default 30%) sit within
   `±TargetBand` of you. Lowest bots go first, so the population drifts upward as
   you level.
2. **Pull the over-leveled down** (optional, `AllowDownleveling`, on by default).
   Any bot above the ceiling is brought back down to a random level between 1 and
   the ceiling. This is what makes a *low* character matter: bring a level-20 onto
   a server full of level-80 bots and they get reshuffled down into the 1–23
   range to play at your level.

A `ProcessLimit` caps how many bots change per pass, so big corrections happen
smoothly over a few passes instead of all at once.

### Example

You log in a single level-20 character on a server with 100 random bots sitting
at levels 60–80, with the defaults (`TargetBand 3`, `TargetPercent 30`):

- The ceiling becomes `20 + 3 = 23`. Every bot above 23 is gradually pulled down
  into `[1, 23]`.
- Roughly 30 of them end up concentrated in your band, `[17, 23]`; the rest
  spread out below.
- Level up, and the band and ceiling rise with you.

### When it runs

The module reacts to **real-player login** and **level-up** events, so bots
adjust the moment you appear or ding — no waiting. A periodic safety timer
(`CheckFrequency`, default 300s) tops things up as bots cycle in and out and
covers the steady state once you're at max level. Set `CheckFrequency = 0` for a
pure event-driven mode.

## Who gets left alone

Some bots are *yours* in a meaningful way, so the module never touches them:

- bots in a **guild** that has any real-player member (online **or** offline),
- bots on a real player's **friends list**,
- bots in an **arena team**,
- bots in **any party or raid group** (so a level isn't changed mid-dungeon),
- bots whose name is listed in `ExcludeNames`.

These checks read existing data only — the module ships **no database tables of
its own**.

On top of those, a bot is never relevelled while it's **busy**: dead, in combat,
in a battleground/arena/random dungeon (or its queue), or in flight. Those are
transient, so the bot is simply adjusted on a later pass once it's free.

## Death Knight progression blocking (optional)

If you run
[mod-individual-progression](https://github.com/ZhengPeiRu21/mod-individual-progression)
and set `DeathKnightProgression.Enabled = 1`, Death Knight bots are kept out of
the world entirely until a real-player account has progressed far enough to
unlock them (`DeathKnightProgression.RequiredState`, default 13 = WotLK content).

It works by toggling mod-playerbots' own `DisableDeathKnightLogin`, so DK bots are
neither created nor logged in while locked — other classes fill those slots, so
your bot count is unaffected. The unlock is **server-wide and sticky**: once any
real account reaches the required state, DKs stay available (across restarts too).
When the option is off, the module leaves mod-playerbots' DK setting alone.

## Configuration

All options live in `mod_rndbot_level_target.conf`. Highlights:

| Option | Default | Meaning |
|--------|---------|---------|
| `RndBotLevelTarget.Enabled` | 1 | Master on/off. |
| `RndBotLevelTarget.TargetPercent` | 30 | Percent of bots to keep in the band. |
| `RndBotLevelTarget.TargetBand` | 3 | Band half-width (± levels) and ceiling offset. |
| `RndBotLevelTarget.AllowDownleveling` | 1 | Pull bots above the ceiling down. 0 = upward-only. |
| `RndBotLevelTarget.ProcessLimit` | 5 | Max bots changed per pass (0 = unlimited). |
| `RndBotLevelTarget.CheckFrequency` | 300 | Safety-timer seconds; 0 = events only. |
| `RndBotLevelTarget.IgnoreGuildBotsWithRealPlayers` | 1 | Skip bots guilded with a real player. |
| `RndBotLevelTarget.IgnoreFriendListed` | 1 | Skip bots on a friends list. |
| `RndBotLevelTarget.IgnoreArenaTeamBots` | 1 | Skip arena-team bots. |
| `RndBotLevelTarget.IgnoreGroupedBots` | 1 | Skip bots in any party/raid group. |
| `RndBotLevelTarget.ExcludeNames` | "" | Comma-separated bot names to skip. |
| `RndBotLevelTarget.DeathKnightProgression.Enabled` | 0 | Gate DK bots on mod-ip progression. |
| `RndBotLevelTarget.DeathKnightProgression.RequiredState` | 13 | Required ProgressionState to unlock DKs. |

See the `.conf.dist` for the full list (including cache-refresh and debug
options).

## Commands

- `.rndbotlevel reload` — reload the configuration at runtime.

## Installation

1. Clone into your AzerothCore `modules/` directory.
2. Re-run CMake and rebuild the server.
3. Copy `mod_rndbot_level_target.conf.dist` to `mod_rndbot_level_target.conf` in
   your config directory and adjust as desired.

Requires mod-playerbots. mod-individual-progression is optional (only for the
Death Knight blocking feature).

## Credits

Derived from
[mod-player-bot-level-brackets](https://github.com/DustinHendrickson/mod-player-bot-level-brackets)
(AGPL-3.0) — the bot-exclusion checks and level-set path come from there. Built on
mod-playerbots and AzerothCore.

## License

AGPL-3.0-or-later. See [`LICENSE`](LICENSE).
