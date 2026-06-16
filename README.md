# mod-rndbot-sync

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

## Item level sync

With `IlvlSync.Enabled = 1` (default), random bots never out-gear you: the
module caps all bot gear at the highest online player's item level. It does this
by driving mod-playerbots' own `AiPlayerbot.RandomGearScoreLimit`, so the cap
applies to freshly initialized bots *and* every re-randomize — gear **quality**
is left untouched.

The player's item level is measured from their **best owned gear** (equipped plus
carried bags, bank excluded), taking the best item per slot. That way a temporary
situational equip — a fishing pole, a gathering tool — doesn't drop the cap,
because your real weapon sitting in a bag still counts. When no real player is
online (or the feature is off), your configured `RandomGearScoreLimit` is
restored.

## Configuration

All options live in `mod_rndbot_sync.conf`. Highlights:

| Option | Default | Meaning |
|--------|---------|---------|
| `RndBotSync.Enabled` | 1 | Master on/off. |
| `RndBotSync.TargetPercent` | 30 | Percent of bots to keep in the band. |
| `RndBotSync.TargetBand` | 3 | Band half-width (± levels) and ceiling offset. |
| `RndBotSync.AllowDownleveling` | 1 | Pull bots above the ceiling down. 0 = upward-only. |
| `RndBotSync.ProcessLimit` | 5 | Max bots changed per pass (0 = unlimited). |
| `RndBotSync.CheckFrequency` | 300 | Safety-timer seconds; 0 = events only. |
| `RndBotSync.IgnoreGuildBotsWithRealPlayers` | 1 | Skip bots guilded with a real player. |
| `RndBotSync.IgnoreFriendListed` | 1 | Skip bots on a friends list. |
| `RndBotSync.IgnoreArenaTeamBots` | 1 | Skip arena-team bots. |
| `RndBotSync.IgnoreGroupedBots` | 1 | Skip bots in any party/raid group. |
| `RndBotSync.ExcludeNames` | "" | Comma-separated bot names to skip. |
| `RndBotSync.DeathKnightProgression.Enabled` | 0 | Gate DK bots on mod-ip progression. |
| `RndBotSync.DeathKnightProgression.RequiredState` | 13 | Required ProgressionState to unlock DKs. |
| `RndBotSync.IlvlSync.Enabled` | 1 | Cap bot gear at the player's item level. |

See the `.conf.dist` for the full list (including cache-refresh and debug
options).

## Commands

All admin-only (console + in-game):

- `.rndbotsync reload` — reload the configuration at runtime.
- `.rndbotsync status` — print a read-only snapshot: the target player, the
  band and ceiling, eligible-bot counts (in-band / above-ceiling / below-band),
  the current gear cap, and the Death Knight gate state.
- `.rndbotsync resync` — force an immediate full pass that ignores the
  `ProcessLimit` throttle (adjusts all eligible bots at once). Still respects the
  ceiling, gear cap, exclusions, and safety skips.

## Installation

1. Clone into your AzerothCore `modules/` directory.
2. Re-run CMake and rebuild the server.
3. Copy `mod_rndbot_sync.conf.dist` to `mod_rndbot_sync.conf` in
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
