#pragma once

// Counter-Strike's bots: waking them at each start, and counting who is up.
// (Who they are -- the stand-ins and their makers -- is HlCs.hpp's; what
// they do, HlCsBots' brain below as it comes.)
//
// std-only and inline, as the rest of the game: the console's game runs it
// (dll/hl/Game.arm.cpp), and so does the host test that plays a real map
// (tests/test_csbots.cpp).

#include <cstdint>

#include "HlCs.hpp"
#include "HlCsRound.hpp"
#include "HlMonsters.hpp"

namespace hldata::cs
{
    /// What wakeBots found of the map's makers, for a test to look at.
    struct BotWake
    {
        int makers = 0;   ///< the bots' makers the map has
        int woken = 0;
        int noPlace = 0;  ///< a maker whose place is not an actor
    };

    /// Wake the round's bots: as many as the setup says on each side, each
    /// at its start; put away any of the last round's still about.
    inline BotWake wakeBots(const LevelView &view, LogicWorld &logic, ActorWorld &actors, std::uint8_t playerTeam,
                            const Setup &setup, BotRoster &out)
    {
        BotWake seen;
        out = BotRoster();
        if (actors.states == nullptr || actors.records == nullptr || logic.world == nullptr ||
            view.nameHashes == nullptr)
            return seen;
        int enemies = 0, mates = 0;
        botSides(setup, playerTeam, enemies, mates);
        const std::uint8_t mine = playerTeam == kTeamT ? kTeamT : kTeamCT;
        const std::uint8_t theirs = mine == kTeamT ? kTeamCT : kTeamT;
        for (int i = 0; i < logic.recordCount; ++i)
        {
            const LogicRecord &r = logic.records[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Maker) || r.name == 0 || r.name >= view.nameCount)
                continue;
            const std::uint32_t hash = view.nameHashes[r.name];
            int which = -1; // 0: theirs (a soldier), 1: the player's (a guard)
            int nth = 0;
            bool ours = false;
            for (int side = 0; side < 2 && !ours; ++side)
            {
                for (int kind = 0; kind < 2 && !ours; ++kind)
                {
                    for (int n = 1; n <= kBotSlotsPerSide && !ours; ++n)
                    {
                        char name[24];
                        const std::uint8_t team = side == 0 ? kTeamT : kTeamCT;
                        botMakerName(name, sizeof(name), team, kind == 0, n);
                        if (nameHash(name) != hash)
                            continue;
                        ours = true;
                        nth = n;
                        if (kind == 0 && team == theirs)
                            which = 0;
                        else if (kind == 1 && team == mine)
                            which = 1;
                    }
                }
            }
            if (!ours)
                continue;
            ++seen.makers;
            const int slot = makerPlace(r, 0);
            if (slot < 0 || slot >= actors.recordCount)
            {
                ++seen.noPlace;
                continue;
            }
            // Last round's, put away.
            ActorState &was = actors.states[slot];
            if (was.active)
            {
                aiForget(actors, slot);
                was.ai = kAiDead;
                was.active = false;
                was.flags &= static_cast<std::uint8_t>(~kActorSolid);
                actorPlaceBox(actors, slot);
            }
            const int wanted = which == 0 ? enemies : (which == 1 ? mates : 0);
            if (which < 0 || nth > wanted)
                continue;
            actorSetup(actors, slot, actors.records[slot], *logic.world);
            actors.states[slot].flags &= static_cast<std::uint8_t>(~kActorDormant);
            aiForget(actors, slot);
            actorPlaceBox(actors, slot);
            if (out.count[which] < kBotSlotsPerSide)
                out.slot[which][out.count[which]++] = slot;
            ++seen.woken;
        }
        return seen;
    }

    /// How many of a side's bots are up: shot down, one lies dying -- a body
    /// -- which is out of the round as much as one blown apart.
    inline int botsAlive(const ActorWorld &actors, const BotRoster &roster, int which)
    {
        int alive = 0;
        for (int n = 0; n < roster.count[which]; ++n)
        {
            const ActorState &bot = actors.states[roster.slot[which][n]];
            alive += bot.active && bot.ai < kAiDying ? 1 : 0;
        }
        return alive;
    }

    // ------------------------------------------------------------ the brain
    //
    // The game's own monster code fights: a bot that sees one of the other
    // side shoots at them, takes cover, chases, as the soldiers and guards
    // always have. What it has not got is the round. That is this: while a
    // bot has nobody to fight it is sent where the round wants it -- the
    // terrorists to a bomb site (one of them with the bomb, which it plants
    // there), the counter-terrorists to hold the sites, and once the bomb is
    // down, everyone to it: the one side to guard it, the other to defuse.
    // The way there is the map's nodes (laid out by the converter on a map
    // that has none: PathNodes.cpp).

    /// Run to a place: the round's errand. Given up for a fight (an enemy
    /// seen, a wound) -- the game's own schedules take over, and the errand
    /// is given again when that is done.
    inline constexpr AiTask tlCsErrand[] = {
        {kTaskStopMoving, 0},
        {kTaskGetPathToSpot, 0},
        {kTaskRunPath, 0},
        {kTaskWaitForMovement, 0},
    };
    inline constexpr AiSchedule slCsErrand = {tlCsErrand, 4,
                                              kCondNewEnemy | kCondSeeEnemy | kCondLightDamage | kCondHeavyDamage,
                                              kSoundDanger, "CS Errand"};
    /// ... pressing on past an enemy it has been at a while to no end (in
    /// sight, out of reach of a shot): only a wound stops it.
    inline constexpr AiSchedule slCsPush = {tlCsErrand, 4, kCondLightDamage | kCondHeavyDamage, kSoundDanger,
                                            "CS Push"};

    /// A fight with nobody hurt for this long is given up for the errand,
    /// for this long.
    inline constexpr int kFightIdleMs = 6000;
    inline constexpr int kPushMs = 6000;
    /// An enemy out of sight this long, its last place this far off, is
    /// let go: the errand again (not hunted across the map). Near, it is
    /// kept -- round the corner.
    inline constexpr int kLostMs = 3000;
    inline constexpr int kLostReach = 640;
    /// Pressing on, an enemy this near is fought (as is any other).
    inline constexpr int kPushNear = 512;

    /// Where each bomb site is to be stood in: the map's nodes inside it
    /// (a few, so that bots sent there spread out).
    inline constexpr int kSitesMost = 2;
    inline constexpr int kSiteSpots = 6;
    struct BotMap
    {
        int sites = 0;
        fx spot[kSitesMost][kSiteSpots][3] = {};
        int spots[kSitesMost] = {};
        std::uint32_t record[kSitesMost] = {}; ///< its BombTarget, in the view's logic
    };

    /// Each bot's own: its errand's spot, how long until it looks again,
    /// and its time at the bomb.
    struct BotMind
    {
        fx goal[3] = {0, 0, 0};
        bool hasGoal = false;
        int againMs = 0;
        int workMs = 0; ///< planting, or defusing
        int grenadeMs = 0; ///< to its next thought of throwing its grenade
        int fightMs = 0;  ///< fighting, with nobody hurt
        int pushMs = 0;   ///< pressing on through a fight that came to nothing
        std::int16_t pushFrom = -1; ///< ... with whom (another, or this one near, is fought)
        int unseenMs = 0; ///< since its enemy was last in sight
        int health = 0, enemyHealth = 0; ///< as they were, to see that
        bool fitted = false; ///< given a player's health
        // Held up on its way (its hull caught where the nodes' walk was
        // not): where it was, how long it has hardly moved, and a node off
        // to one side to go to a while before trying again.
        fx was[3] = {0, 0, 0};
        int stillMs = 0;
        fx detour[3] = {0, 0, 0};
        int detourMs = 0;
        int tries = 0;
        // Its footsteps: where it was a frame ago, and the ms to the next.
        fx stepFrom[3] = {0, 0, 0};
        int stepLeft = 0;
        bool stepKnown = false;
        bool wasUp = true; ///< (its death heard once)
        bool viaDone = false; ///< past its way round (BotBrain::via), or not sent that way
        int viaStep = 0;      ///< ... the node of it it is going to
        int viaMs = 0;        ///< ... on its way there (given up on, after a while)
        bool viaFresh = false; ///< told another way mid-round: taken up where it is
        std::int8_t crawlDuct = -1; ///< crawling through a duct (BotBrain::duct; -1: not)
        std::int8_t crawlAt = 0;    ///< ... to its point this
        // What it heard (or was told): where, how long it goes to look
        // there, and how long it is turned to face it -- the errand held
        // off meanwhile. A noise is taken in after its reaction time.
        fx heard[3] = {0, 0, 0};
        int heardMs = 0;   ///< going to look, this much longer (0: not)
        int faceMs = 0;    ///< turned to face something, this much longer
        int reactMs = -1;  ///< to a noise it has just heard (-1: none)
        int noise = -1;    ///< ... which (BotEars' slot)
        int lookMs = 0;    ///< to its next look round, holding a place
        bool atStage = false; ///< at its team's gathering place (TeamPlan)
        int deafMs = 0;       ///< just took a noise in: the next, after this
        int stuckMs = 0;      ///< hardly moved, on its way, this long in all
        int stuckLink = -1;   ///< the link it was last held up on ...
        int stuckTimes = 0;   ///< ... and how often, in a row
    };

    inline bool botUp(const ActorState &bot) { return bot.active && bot.ai < kAiDying; }

    /// Everyone in Counter-Strike has a hundred: the soldier and the guard
    /// stood in for a bot are given it, not their own.
    inline constexpr int kBotHealth = 100;

    // ------------------------------------------------------------ the guns
    //
    // A bot plays the money as the player does: it starts with its side's
    // pistol and its starting money, is paid at each round's end as the
    // player is (more for a win, the losing streak's bonus for a loss, the
    // terrorists' 800 for a bomb planted), and buys its side's rifle as soon
    // as it can afford it. One that lives through a round keeps what it has;
    // one shot down starts again with the pistol. What it has is what it is
    // seen holding (its p_ model) and what its rounds do.
    inline constexpr int kBotGunPrices[kBotGuns] = {0, 0, 2500, 3100, 2200};
    /// What a round of each does to an unarmoured body (Counter-Strike
    /// 1.6's), and how much of armour it goes through (hundredths).
    inline constexpr int kBotGunDamage[kBotGuns] = {25, 34, 36, 32, 34};
    inline constexpr int kBotGunPierce[kBotGuns] = {105, 100, 155, 140, 100};
    /// How far a bot's rounds stray, by the setup's skill (easy .. expert):
    /// the cone, Q12. (Skill is aim, as Counter-Strike's bots have it; what
    /// a round does is the gun's.)
    inline constexpr int kBotSkillCone[kBotSkills] = {260, 170, 110, 70};

    inline BotGun botPistol(std::uint8_t team) { return team == kTeamT ? kGunGlock : kGunUsp; }
    inline BotGun botRifle(std::uint8_t team) { return team == kTeamT ? kGunAk47 : kGunM4a1; }

    /// A bot's money and gun: kept from round to round, as the player's.
    struct BotPurse
    {
        int money = 0;
        BotGun gun = kGunGlock;
        std::uint8_t armor = 0; ///< 0 none, 1 kevlar, 2 kevlar and helmet
        std::uint8_t grenade = kGearNone; ///< one grenade, as the game's bots buy (kGearHe, kGearFlash, kGearSmoke)
    };

    inline constexpr int kLanesMost = 4;
    inline constexpr int kLaneSteps = 4; ///< a way's nodes, in turn
    inline constexpr int kHoldsMost = 10;
    inline constexpr int kCtSetups = 4;
    struct BotBrain
    {
        BotMap map;
        BotMind mind[2][kBotSlotsPerSide];
        BotPurse purse[2][kBotSlotsPerSide];
        int site = 0;     ///< the terrorists' choice this round
        bool paid = true; ///< this round's end paid out
        int skill = 1;    ///< the setup's bot skill
        const LevelView *view = nullptr; ///< the map's (botsNewRound): for its sounds
        // The other way to each site: a node well off the shortest way from
        // the terrorists' spawn, on one not much longer (de_dust2's long A
        // beside its catwalk) -- found once a map, as a round first goes live.
        fx via[kSitesMost][3] = {};
        bool viaOk[kSitesMost] = {};
        // ... all of them (lanes): a node each, the way through it in; the
        // shortest way is lane 0, these 1 on.
        fx lane[kSitesMost][kLanesMost][kLaneSteps][3] = {};
        int laneSteps[kSitesMost][kLanesMost] = {};
        int autoLanes[kSitesMost] = {}; ///< the first of them, those the bots take of themselves (the rest: told)
        int lanes[kSitesMost] = {};
        /// The shortest way is one of the choices too (lane -1); not where
        /// the map's own ways are known (KnownMap).
        bool shortestToo[kSitesMost] = {};
        // A known map's counter-terrorists' places (KnownMap): each where to
        // stand, what to watch, its site, and whether only early on.
        fx hold[kHoldsMost][3] = {};
        fx holdLook[kHoldsMost][3] = {};
        int holdSite[kHoldsMost] = {};
        bool holdEarly[kHoldsMost] = {};
        int holds = 0;
        int setup[kCtSetups][kBotSlotsPerSide] = {}; ///< holds, as KnownMap::setups
        bool viaMade = false;
        // ... and on the shortest way, where the terrorists gather before
        // they go in (some 700 units short of the site), as they come.
        fx stage[kSitesMost][3] = {};
        bool stageOk[kSitesMost] = {};
        // Where each team starts (the map's starts).
        fx tHome[3] = {0, 0, 0}, ctHome[3] = {0, 0, 0};
        // Which of the map's sites are A and B (a known map's; else the
        // first two), for the player's orders.
        int siteA = 0, siteB = 1;
        // A known map's ways, as they were put on it: each route's site and
        // way (-1: left out), and the radio's named ways' routes.
        std::int8_t routeSite[8] = {}, routeLane[8] = {};
        std::int8_t wayRoute[kRadioWaysMost] = {};
        // ... and the counter-terrorists' places the radio names: two holds
        // each (-1: none).
        std::int8_t ctWayHold[kRadioWaysMost][2] = {};
        // Its ducts (a vent; a stair or a drop no node goes by): crawled
        // through, point to point -- the way in first, the way out last --
        // at their pace (units a second).
        fx duct[2][6][3] = {};
        int ductLen[2] = {};
        int ductPace[2] = {};
        int ducts = 0;
        // The orders, each team's (HlCs.hpp: teamOrder, by teamSlot): those
        // put to the bots, where each was told to hold, and the way of the
        // one who gave them (a point each 80 units walked, the latest last)
        // to follow.
        int orderSeen[2] = {};
        fx held[2][kBotSlotsPerSide][3] = {};
        fx trail[2][kBotSlotsPerSide + 1][3] = {};
        int trailCount[2] = {};
        bool leaderUp[2] = {};
        bool homes = false;
        int fetcher = -1; ///< the terrorist bot sent for the bomb, lying (-1: none)
        // The terrorists' play this round, and their counter-terrorists'
        // places (TeamPlan, CtPlan).
        std::uint32_t seed = 1;
    };
    constinit inline BotBrain brain;

    // ------------------------------------------------------------ the plays
    //
    // The terrorists pick a play each round, as a team: a rush (all in at
    // one site, at once), a split (half the long way round, the rest the
    // short, in together), a default (spread out over the map, a while, then
    // in where the counter-terrorists were seen least) or a fake (shown at
    // one site, then in at the other). Short of money they save (no rifles,
    // no armour) and rush. Late in the round, whatever the play, they go in.
    //
    // The counter-terrorists split over the sites (which has more, by the
    // round); each site's first is its anchor, who stays put. One may play
    // forward a while, where the terrorists come from. A site with
    // terrorists seen at it -- or heard -- is called, and the others, the
    // anchors aside, rotate to it. A bomb down, all go to it.
    enum class Play : std::uint8_t
    {
        Rush,
        Split,
        Default,
        Fake
    };
    struct TeamPlan
    {
        Play play = Play::Split;
        int site = 0;          ///< where they go in
        int fake = 0;          ///< ... where they show, first (a fake)
        bool eco = false;      ///< saving: no buying
        bool committed = false; ///< going in now
        int holdMs = 0;        ///< gathered (or shown, a fake) this long
        int commitAtMs = 0;    ///< a default: when to go in, at the latest
        int seen[kSitesMost] = {}; ///< counter-terrorists seen by each site
        std::uint32_t roll = 0;    ///< the round's dice: whose way is which
        int lurker = -1;           ///< which of them lurks (-1: none)
        int lane = -1;             ///< the way all take in, told (the radio's: Order::Way*); -1: their own
    };
    struct CtPlan
    {
        int site[kBotSlotsPerSide] = {};   ///< each one's site
        bool anchor[kBotSlotsPerSide] = {}; ///< ... and the one who stays at it
        bool forward[kBotSlotsPerSide] = {}; ///< plays forward, early on
        int setup = 0; ///< a known map's: which of its setups (KnownMap)
        bool eco = false;
        int called = -1;  ///< a site the terrorists were seen at (-1: none)
        int calledMs = 0; ///< ... when (the round's live time)
        // The bomb down: they gather short of it (on their side), then go in
        // together -- or at once, the bomb near going off.
        bool retakeGo = false;
        bool retakeMade = false;
        fx retake[3] = {0, 0, 0};
        // A counter-terrorist player's orders (HlCs.hpp's Order, theirs):
        // the site the bomb is said to be going to (-1: none), forward,
        // playing for the retake, and a place of the map's to hold (-1).
        int told = -1;
        bool push = false;
        bool forRetake = false;
        int wayHold = -1;
    };
    constinit inline TeamPlan tPlan;
    constinit inline CtPlan ctPlan;
    inline std::uint32_t botRandom()
    {
        brain.seed = brain.seed * 1664525u + 1013904223u;
        return brain.seed >> 8;
    }

    // ------------------------------------------------------------ the ears
    //
    // What is heard: shots (the player's, the bots'), running feet, the bomb
    // being planted. Each a while, with who made it and their team. A bot
    // hears an enemy's within its reach (a shot far off, feet nearer), turns
    // to it after its reaction time (its skill's), and, near enough, goes to
    // look. A teammate's sight of an enemy is told to the others near as a
    // noise of its own.
    struct Noise
    {
        bool active = false;
        fx pos[3] = {0, 0, 0};
        std::uint8_t team = 0;
        std::uint8_t loud = 0; ///< 0 feet, 1 a shot, 2 the bomb, 3 a call (an enemy seen), 4 a teammate down
        std::int16_t who = -1;
        int ageMs = 0;
    };
    inline constexpr int kNoises = 16;
    inline constexpr int kNoiseMs = 1200;
    inline constexpr int kHearFeet = 900, kHearShot = 2200, kHearBomb = 1400, kHearCall = 1800;
    inline constexpr int kBotReactMs[kBotSkills] = {700, 450, 280, 160};
    struct BotEars
    {
        Noise noise[kNoises];
        std::uint8_t botFired[kBotGunActors] = {}; ///< csShotSoundHook: a bot shot, this frame
        bool playerFired = false;                 ///< csFireSoundHook: the player shot
        int next = 0;
    };
    constinit inline BotEars ears;
    inline void hearNoise(const fx *pos, int who, std::uint8_t team, std::uint8_t loud)
    {
        // (The same one's, again: only freshened.)
        for (Noise &n : ears.noise)
        {
            if (n.active && n.who == who && n.loud == loud)
            {
                for (int k = 0; k < 3; ++k)
                    n.pos[k] = pos[k];
                n.ageMs = 0;
                return;
            }
        }
        Noise &n = ears.noise[ears.next++ % kNoises];
        n.active = true;
        for (int k = 0; k < 3; ++k)
            n.pos[k] = pos[k];
        n.team = team;
        n.loud = loud;
        n.who = static_cast<std::int16_t>(who);
        n.ageMs = 0;
    }
    inline int hearReach(std::uint8_t loud)
    {
        return loud == 1 ? kHearShot : (loud == 2 ? kHearBomb : (loud >= 3 ? kHearCall : kHearFeet));
    }

    // ------------------------------------------------------------ grenades
    //
    // The player's hand grenades are the stand-in's: one count of them all
    // (Inventory::grenades), thrown by its code, a blast when the fuse is
    // out. PlayerCs keeps how many of each kind (HlCs.hpp's buy) and which
    // is in hand. Here: a throw seen -- one fewer in the count -- is "Fire in
    // the hole!", and the projectile marked with its kind, so that when it
    // goes off (csGrenadeHook) a flashbang flashes and a smoke grenade
    // smokes -- neither of them a blast.

    inline fx flatApart(const fx *p, const fx *q);

    struct Grenades
    {
        std::uint32_t flying = 0; ///< the projectile slots with a grenade, last frame
        std::uint32_t fromBot = 0; ///< ... those a bot threw (botThrow): not the player's
        std::int16_t by[kMaxProjectiles] = {}; ///< ... who threw each (an actor; kAiPlayer)
        std::uint8_t kind[kMaxProjectiles] = {}; ///< ... each one's (a Gear; kGearHe a blast)
    };
    constinit inline Grenades grenades;

    /// Whether a line from `from` to `to` goes through a cloud thick
    /// enough to hide what is behind (csSightHook).
    inline bool smokeBetween(const fx *from, const fx *to)
    {
        for (const SmokeCloud &cloud : smokeClouds)
        {
            const int radius = smokeRadius(cloud) * 4 / 5;
            if (radius <= 0 || smokeThickness(cloud) < 16)
                continue;
            // (Its middle, a man's height over where it went off.)
            const std::int64_t c[3] = {cloud.at[0] >> kFxShift, cloud.at[1] >> kFxShift, (cloud.at[2] >> kFxShift) + 48};
            const std::int64_t a[3] = {from[0] >> kFxShift, from[1] >> kFxShift, from[2] >> kFxShift};
            const std::int64_t d[3] = {(to[0] >> kFxShift) - a[0], (to[1] >> kFxShift) - a[1], (to[2] >> kFxShift) - a[2]};
            const std::int64_t length = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            const std::int64_t along = (c[0] - a[0]) * d[0] + (c[1] - a[1]) * d[1] + (c[2] - a[2]) * d[2];
            // The nearest point of the line to its middle (in 1024ths of the line).
            const std::int64_t t = length <= 0 ? 0 : (along <= 0 ? 0 : (along >= length ? 1024 : along * 1024 / length));
            std::int64_t gap = 0;
            for (int k = 0; k < 3; ++k)
            {
                const std::int64_t e = a[k] + d[k] * t / 1024 - c[k];
                gap += e * e;
            }
            if (gap < static_cast<std::int64_t>(radius) * radius)
                return true;
        }
        return false;
    }

    /// The bomb gone off at `at`: its fireball, its smoke and the shake, and
    /// what it does to the bots -- of either side, as to the player
    /// (blastAt: the game's bomb reaches through walls). Another console's
    /// player is not hurt here: their own console works out theirs.
    inline void bombGoesOff(LogicWorld &w, PlayerState &player, const fx *at, int radius)
    {
        explodeAt(w, player, at, 300, true, false, false, 40, true, nullptr);
        if (w.actors == nullptr)
            return;
        ActorWorld &a = *w.actors;
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (!actor.active || actor.ai >= kAiDying || !canBeHurt(actor.kind) ||
                actor.kind == static_cast<std::uint8_t>(ActorKind::Player))
                continue;
            const int dx = fxToInt(actor.pos[0] - at[0]), dy = fxToInt(actor.pos[1] - at[1]),
                      dz = fxToInt(actor.pos[2] - at[2]);
            const int hurt = blastAt(radius, isqrt(static_cast<std::uint32_t>(dx * dx + dy * dy + dz * dz)));
            if (hurt > 0)
                monsterHurt(w, i, hurt, false, kHitBody, kDmgBlast, kAiNone, at);
        }
    }

    /// A smoke grenade gone off at `at`: its cloud (the oldest makes way).
    inline void smokeGoesOff(LogicWorld &w, const fx *at)
    {
        SmokeCloud *cloud = &smokeClouds[0];
        for (SmokeCloud &c : smokeClouds)
            if (c.ms <= 0 || c.ms > cloud->ms)
                cloud = &c;
        for (SmokeCloud &c : smokeClouds)
            if (c.ms <= 0)
            {
                cloud = &c;
                break;
            }
        for (int k = 0; k < 3; ++k)
            cloud->at[k] = at[k];
        cloud->ms = 1;
        if (w.heard != nullptr && brain.view != nullptr)
        {
            const std::uint16_t hiss = findSound(*brain.view, CsSound::SmokeBurst);
            if (hiss != kNoSound)
                w.heard->add(hiss, at, 127, kRadiusLarge);
        }
    }

    /// The clouds thinning, and the view grey in one.
    inline void smokeTick(LogicWorld &w, PlayerState &state, int ms)
    {
        for (SmokeCloud &cloud : smokeClouds)
            if (cloud.ms > 0 && (cloud.ms += ms) >= kSmokeGoneMs)
                cloud.ms = 0;
        // (In one: the view grey, as thick as it is -- held while there, and
        // fading as they come out. Not over a flashbang's white.)
        if (state.health > 0)
        {
            const fx eye[3] = {state.origin[0], state.origin[1], state.origin[2] + fxFromInt(28)};
            int thick = 0;
            for (const SmokeCloud &cloud : smokeClouds)
            {
                const fx middle[3] = {cloud.at[0], cloud.at[1], cloud.at[2] + fxFromInt(48)};
                const fx d[3] = {eye[0] - middle[0], eye[1] - middle[1], eye[2] - middle[2]};
                if (smokeRadius(cloud) > 0 && fxLength(d) < fxFromInt(smokeRadius(cloud) * 3 / 4))
                    thick = smokeThickness(cloud) > thick ? smokeThickness(cloud) : thick;
            }
            constexpr std::uint16_t kGrey = 21 | 21 << 5 | 21 << 10;
            if (thick > 0 && (!w.fading || w.fadeColour.value == kGrey))
            {
                w.fadeFrom = true;
                w.fadeColour.value = kGrey;
                w.fadeDark = static_cast<std::uint8_t>(230 * thick / 31);
                w.fadeHoldMs = 100;
                w.fadeMs = -w.fadeHoldMs;
                w.fadeWholeMs = 500;
                w.fadeLevel = w.fadeDark;
                w.fading = true;
            }
        }
    }

    /// Everything of the grenades' gone, for a new round or a new map.
    inline void grenadesClear()
    {
        grenades = Grenades();
        for (SmokeCloud &cloud : smokeClouds)
            cloud = SmokeCloud();
    }

    inline void grenadesTick(LogicWorld &w, PlayerState &state, PlayerCs &cs, int ms = 33)
    {
        if (w.actors == nullptr)
            return;
        std::uint32_t flying = 0;
        for (int k = 0; k < kMaxProjectiles; ++k)
        {
            const Projectile &shot = w.actors->shots[k];
            if (shot.active && shot.kind == kShotGrenade)
                flying |= 1u << k;
        }
        const std::uint32_t fresh = flying & ~grenades.flying & ~grenades.fromBot;
        // (A slot's kind is kept until it is thrown again: a match tells of
        // it once it has gone off -- netBurstKind.)
        for (int k = 0; k < kMaxProjectiles; ++k)
            if (fresh & (1u << k))
                grenades.kind[k] = kGearNone;
        const int carried = grenadesCarried(cs);
        if (state.inv.grenades < carried)
        {
            // Thrown: the one in hand.
            const std::uint8_t kind = grenadeInHand(cs);
            if (kind != kGearNone)
            {
                grenadesOf(cs, kind) -= 1;
                for (int k = 0; k < kMaxProjectiles; ++k)
                    if (fresh & (1u << k))
                    {
                        grenades.kind[k] = kind;
                        grenades.by[k] = kAiPlayer;
                    }
                grenadeGone(cs, kind);
            }
            if (w.heard != nullptr && brain.view != nullptr)
            {
                const std::uint16_t say = findSound(*brain.view, CsSound::FireInTheHole);
                if (say != kNoSound)
                    w.heard->add(say, nullptr, 127, 0);
            }
        }
        if (state.inv.grenades <= 0)
            cs.he = cs.flashes = cs.smokes = 0;
        else if (state.inv.grenades > grenadesCarried(cs))
            cs.he = state.inv.grenades - cs.flashes - cs.smokes; // (one picked up: an HE)
        cs.hand = grenadeInHand(cs) != kGearNone ? grenadeInHand(cs) : cs.hand;
        grenades.flying = flying;
        grenades.fromBot &= flying;
        smokeTick(w, state, ms);
    }

    /// A flashbang gone off at `at`: its bang, and whoever sees it blinded:
    /// the player white for a while, longer the nearer and the more they
    /// faced it. (Not a blast.)
    inline void flashAt(LogicWorld &w, PlayerState &state, const fx *at)
    {
        if (w.heard != nullptr && brain.view != nullptr)
        {
            static int which = 0;
            const std::uint16_t bang =
                findSound(*brain.view, (which++ & 1) ? CsSound::Flash2 : CsSound::Flash1);
            if (bang != kNoSound)
                w.heard->add(bang, at, 127, kRadiusLarge);
        }
        if (state.health <= 0 || w.world == nullptr)
            return;
        const fx eye[3] = {state.origin[0], state.origin[1], state.origin[2] + fxFromInt(28)};
        const fx to[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
        const int far = fxToInt(fxLength(to));
        if (far > 1500 || !lineClear(*w.world, eye, at) || smokeBetween(eye, at))
            return;
        // (Faced: the way the player looks, flat, against the way to it.)
        const fx flat[3] = {to[0], to[1], 0};
        const fx across = fxLength(flat);
        const std::int64_t dot = across > 0 ? (static_cast<std::int64_t>(state.facingXY[0]) * to[0] +
                                               static_cast<std::int64_t>(state.facingXY[1]) * to[1]) /
                                                  across
                                            : kFxOne;
        const bool faced = dot > kFxOne * 3 / 10;
        const int near = 1500 - far; // 0..1500
        w.fadeFrom = true;
        w.fadeColour.value = 0x7FFF;
        w.fadeDark = static_cast<std::uint8_t>(faced ? 255 : 120 + near * 80 / 1500);
        w.fadeHoldMs = faced ? 800 + near * 1700 / 1500 : 100;
        w.fadeMs = -w.fadeHoldMs;
        w.fadeWholeMs = faced ? 1500 + near * 1500 / 1500 : 700;
        w.fadeLevel = w.fadeDark;
        w.fading = true;
    }

    /// What kind of grenade went off in projectile slot `slot` (kGearHe if
    /// none of the others): kept until the slot is thrown again, for a
    /// match's word of it (netBurstKind).
    inline std::uint8_t grenadeKindOf(int slot)
    {
        return slot >= 0 && slot < kMaxProjectiles && grenades.kind[slot] != kGearNone ? grenades.kind[slot] : kGearHe;
    }

    /// An HE gone off at `at`, thrown by `by` (an actor, or kAiPlayer): as
    /// 1.6's -- 100 at its middle, falling off evenly to nothing at 350
    /// units (its RadiusDamage: three and a half times the damage), through
    /// walls. Not on the thrower's own side (no friendly fire), but on the
    /// thrower themself.
    inline constexpr int kHeDamage = 100;
    inline void heGoesOff(LogicWorld &w, PlayerState &state, const fx *at, std::int16_t by)
    {
        explodeAt(w, state, at, kHeDamage, true, true, false, grenadeSmoke(kHeDamage), true, nullptr);
        const int byTeam = by == kAiPlayer ? player.team + 1 : (by >= 0 && by < kBotGunActors ? botTeamOf[by] : 0);
        const auto hurtAt = [&](const fx *where) -> int
        {
            const int dx = fxToInt(where[0] - at[0]), dy = fxToInt(where[1] - at[1]), dz = fxToInt(where[2] - at[2]);
            return blastAt(kHeDamage, isqrt(static_cast<std::uint32_t>(dx * dx + dy * dy + dz * dz)));
        };
        if (state.health > 0 && (by == kAiPlayer || byTeam != player.team + 1))
            if (const int hurt = hurtAt(state.origin); hurt > 0)
            {
                if (by != kAiPlayer)
                    csPlayerHitBy = by;
                playerTakeDamage(state, hurt, kDmgBlast, at);
            }
        if (w.actors == nullptr)
            return;
        ActorWorld &a = *w.actors;
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (!actor.active || actor.ai >= kAiDying || !canBeHurt(actor.kind) ||
                actor.kind == static_cast<std::uint8_t>(ActorKind::Player))
                continue;
            const int team = i < kBotGunActors ? botTeamOf[i] : 0;
            if (i != by && team != 0 && team == byTeam)
                continue;
            if (const int hurt = hurtAt(actor.pos); hurt > 0)
                monsterHurt(w, i, hurt, false, kHitBody, kDmgBlast, by, at);
        }
    }

    /// A grenade of kind `kind` gone off at `at`, thrown by `by`: what it
    /// does. Whether it was one of Counter-Strike's (an HE from another
    /// console, kind 0: the stand-in's blast).
    inline bool grenadeEffect(LogicWorld &w, PlayerState &state, std::uint8_t kind, const fx *at, std::int16_t by)
    {
        switch (kind)
        {
        case kGearFlash: flashAt(w, state, at); return true;
        case kGearSmoke: smokeGoesOff(w, at); return true;
        default: return false;
        }
    }

    /// csGrenadeHook: a grenade of this console's gone off -- the player's
    /// or a bot's: an HE's blast as the game's, or one of the others.
    inline bool grenadeGoesOff(LogicWorld &w, PlayerState &state, int slot, const fx *at)
    {
        if (slot < 0 || slot >= kMaxProjectiles)
            return false;
        const std::uint8_t kind = grenades.kind[slot];
        const std::int16_t by = (grenades.fromBot & (1u << slot)) ? grenades.by[slot] : kAiPlayer;
        if (kind == kGearHe)
        {
            heGoesOff(w, state, at, by);
            return true;
        }
        return grenadeEffect(w, state, kind, at, by);
    }

    inline int botGrenadesThrown = 0; ///< (for the tests)
    /// A bot's grenade (`kind`) thrown from `from` at `velocity`: marked as
    /// the bot's, to go off as its kind. False: no room for it.
    inline bool botThrow(ActorWorld &a, int index, std::uint8_t kind, const fx *from, const fx *velocity)
    {
        // (The game's fuse: a second and a half, each kind.)
        Projectile *shot = projectileAdd(a, kShotGrenade, from, velocity, 1500);
        if (shot == nullptr)
            return false;
        const int slot = static_cast<int>(shot - a.shots);
        grenades.kind[slot] = kind;
        grenades.by[slot] = static_cast<std::int16_t>(index);
        grenades.fromBot |= 1u << slot;
        grenades.flying |= 1u << slot;
        ++botGrenadesThrown;
        return true;
    }

    /// csSightHook: a bot's line of sight, through a cloud of smoke: none.
    inline bool smokeHides(const fx *from, const fx *to) { return smokeBetween(from, to); }

    /// netBurstKindHook: the kind said with a Burst of this console's (0:
    /// an HE's blast).
    inline int netBurstKind(int slot)
    {
        const std::uint8_t kind = grenadeKindOf(slot);
        return kind == kGearHe ? 0 : kind;
    }
    /// netBurstHook: another's grenade of kind `kind` gone off here.
    inline bool netBurst(LogicWorld &w, PlayerState &state, int kind, const fx *at, std::int16_t by)
    {
        return grenadeEffect(w, state, static_cast<std::uint8_t>(kind), at, by);
    }

    /// The gun the actor is a bot holding (kBotGuns: not a bot).
    inline int botGunOf(int actor)
    {
        return actor >= 0 && actor < kBotGunActors && botGunHeld[actor] != 0 ? botGunHeld[actor] - 1 : kBotGuns;
    }
    /// What a round of a bot's does (csShotDamage): its gun's, by the
    /// skill; anyone else's, their own.
    inline int botShotDamage(int shooter, int base)
    {
        const int gun = botGunOf(shooter);
        if (gun >= kBotGuns)
            return base;
        // (What the player's armour lets through of it, if it is the
        // player this round is for.)
        csArmorPierce = kBotGunPierce[gun];
        return kBotGunDamage[gun];
    }
    /// How far its rounds stray (csShotConeHook): by the setup's skill.
    inline int botShotCone(int shooter, int base)
    {
        if (botGunOf(shooter) >= kBotGuns)
            return base;
        const int skill = brain.skill >= 0 && brain.skill < kBotSkills ? brain.skill : 1;
        return kBotSkillCone[skill];
    }

    /// The gun in the player's hand: the item bought that `weapon` stands
    /// in for (kNoItem: none -- the knife, a grenade, the bomb).
    inline int heldItem(std::uint8_t weapon)
    {
        int item = kNoItem;
        if (weapon == kWeaponGlock || weapon == kWeaponPython)
            item = player.pistol;
        else if (weapon == kWeaponMp5 || weapon == kWeaponShotgun || weapon == kWeaponCrossbow)
            item = player.primary;
        return item >= 0 && item < kItemCount && kItems[item].standIn == weapon ? item : kNoItem;
    }

    /// csGunKindHook: the stand-in's gun with the bought one's numbers
    /// (HlCs.hpp's kGuns): its magazine, rate, damage, reload, pellets,
    /// spread. (A sniper's bolt does the bolt's: the crossbow's code.)
    inline const GunKind *csGunKind(std::uint8_t weapon)
    {
        const int item = heldItem(weapon);
        const CsGun *g = gunOfItem(item);
        if (g == nullptr)
            return nullptr;
        static GunKind kinds[kItemCount];
        static bool made[kItemCount] = {};
        if (!made[item])
        {
            made[item] = true;
            GunKind k = gunKindOwn(weapon);
            k.holds = g->clip;
            if (weapon != kWeaponShotgun)
                k.reloadMs = g->reloadMs; // (a shotgun's goes by the shell, as its code has it)
            k.refireMs = g->refireMs;
            // (The snipers too: their round is a round here, not the bolt
            // -- an AWP's 115 is one shot to the body.)
            k.damage = g->damage;
            k.pellets = g->pellets;
            if (weapon != kWeaponCrossbow)
                k.cone = g->cone;
            k.range = 8192;
            kinds[item] = k;
        }
        // The silencer on: the M4A1's round a little harder (33) and wider
        // (by a quarter), the USP's softer (30) and wider (by a half), as
        // 1.6's.
        if (silencedInHand(weapon, player))
        {
            static GunKind silenced[2];
            const bool m4 = std::strcmp(kItems[item].name, "M4A1") == 0;
            GunKind &k = silenced[m4 ? 0 : 1];
            k = kinds[item];
            k.damage = m4 ? 33 : 30;
            k.cone = m4 ? kinds[item].cone * 5 / 4 : kinds[item].cone * 3 / 2;
            return &k;
        }
        return &kinds[item];
    }

    /// csFireSoundHook: the bought gun's own shot, for the stand-in in
    /// hand (kNoSound: the stand-in's).
    inline std::uint16_t csFireSound(std::uint8_t weapon)
    {
        const CsGun *g = gunOfItem(heldItem(weapon));
        // (Silenced: its own quiet shot, which the bots do not hear across
        // the map.)
        if (g != nullptr && silencedInHand(weapon, player))
            return silencedSounds[std::strcmp(g->item, "M4A1") == 0 ? 0 : 1];
        ears.playerFired = true;
        return g != nullptr ? gunSounds[g - kGuns] : kNoSound;
    }
    inline constexpr int kGunOfBotGun[kBotGuns] = {0, 1, 15, 16, 1}; // Glock, USP, AK-47, M4A1, USP in kGuns
    /// csFalloffHook: what is left of a round after `distance` units, by
    /// the gun that fired it -- a bot's, or the player's in hand.
    inline int falloff(int shooter, std::uint8_t weapon, int damage, int distance)
    {
        const CsGun *g = nullptr;
        if (shooter < 0)
        {
            g = gunOfItem(heldItem(weapon));
            // (The M4A1 silenced: RANGE_MODIFER_SIL, .95.)
            if (g != nullptr && silencedInHand(weapon, player) && std::strcmp(g->item, "M4A1") == 0)
            {
                CsGun quiet = *g;
                quiet.rangeMod = 950;
                return damageAfter(quiet, damage, distance);
            }
        }
        else if (const int gun = botGunOf(shooter); gun < kBotGuns)
            g = &kGuns[kGunOfBotGun[gun]];
        return g != nullptr ? damageAfter(*g, damage, distance) : damage;
    }
    /// csShotSoundHook: a bot's gun's shot.
    inline std::uint16_t botShotSound(int shooter)
    {
        const int gun = botGunOf(shooter);
        if (shooter >= 0 && shooter < kBotGunActors)
            ears.botFired[shooter] = 1;
        return gun < kBotGuns ? gunSounds[kGunOfBotGun[gun]] : kNoSound;
    }

    /// What a round from `attacker` goes through of armour (hundredths).
    inline int pierceOf(int attacker)
    {
        const int gun = botGunOf(attacker);
        if (gun < kBotGuns)
            return kBotGunPierce[gun];
        if (attacker == kAiPlayer)
        {
            // (The player's: the gun in hand, if one was bought.)
            for (const CsGun &g : kGuns)
            {
                const int item = player.primary >= 0 ? player.primary : player.pistol;
                if (item >= 0 && std::strcmp(g.item, kItems[item].name) == 0)
                    return g.armorPierce;
            }
        }
        return 100;
    }

    /// csTakeHook: what a bot takes of a blow, as Counter-Strike has it --
    /// by where it landed (the head four times, the stomach 1.25, a leg
    /// .75), and then its armour: on the body (the head too, with a
    /// helmet) half of it, more through it by the gun.
    /// (What botTakes sees of where a blow came from: botsThink's world,
    /// and the player's place.)
    inline const ActorWorld *takeWorld = nullptr;
    inline fx takePlayer[3] = {0, 0, 0};
    inline int botTakes(int victim, int attacker, int amount, std::uint8_t group)
    {
        if (victim >= 0 && victim < 256 && amount > 0)
        {
            scores.hitBy[victim] = static_cast<std::int16_t>(attacker);
            // (Kept past the scoreboard's count of it: a match tells who.)
            csActorHitBy[victim] = static_cast<std::int16_t>(attacker);
        }
        // (The game's hit groups: 1 the head, 2 the chest, 3 the stomach,
        // 4 and 5 the arms, 6 and 7 the legs.)
        int hit = amount * 100;
        if (group == 1)
            hit *= 4;
        else if (group == 3)
            hit = hit * 5 / 4;
        else if (group == 6 || group == 7)
            hit = hit * 3 / 4;
        // (A bot's tactical shield: what comes at it from in front, but
        // for the head, is stopped by it.)
        if (botGunOf(victim) == kGunShield && takeWorld != nullptr && victim < takeWorld->count && group != 1)
        {
            const fx *from = attacker == kAiPlayer ? takePlayer
                             : (attacker >= 0 && attacker < takeWorld->count ? takeWorld->states[attacker].pos : nullptr);
            if (from != nullptr)
            {
                const ActorState &me = takeWorld->states[victim];
                fx forward[3];
                aiForward(me.yaw, forward);
                const fx flat[3] = {from[0] - me.pos[0], from[1] - me.pos[1], 0};
                const fx far = fxLength(flat);
                const std::int64_t along = static_cast<std::int64_t>(flat[0]) * forward[0] + static_cast<std::int64_t>(flat[1]) * forward[1];
                if (far > 0 && along / far >= 3355)
                    return 0;
            }
        }
        // The bot's armour.
        int armor = 0;
        const int gunOfVictim = botGunOf(victim);
        if (gunOfVictim < kBotGuns)
        {
            for (int side = 0; side < 2 && armor == 0; ++side)
                for (int n = 0; n < roster.count[side]; ++n)
                    if (roster.slot[side][n] == victim)
                        armor = brain.purse[side][n].armor;
        }
        const bool covered = armor > 0 && group != 6 && group != 7 && (group != 1 || armor >= 2);
        if (covered)
        {
            int ratio = 50 * pierceOf(attacker) / 100; // (the game's 0.5 times the gun's modifier)
            ratio = ratio > 100 ? 100 : ratio;
            hit = hit * ratio / 100;
        }
        hit /= 100;
        return hit > 0 ? hit : (amount > 0 ? 1 : 0);
    }

    inline bool inSiteBox(const LogicRecord &r, const fx *feet, int grow)
    {
        for (int a = 0; a < 2; ++a)
        {
            if (feet[a] < fxFromInt(r.mins[a] - grow) || feet[a] > fxFromInt(r.maxs[a] + grow))
                return false;
        }
        return feet[2] >= fxFromInt(r.mins[2] - 48) && feet[2] <= fxFromInt(r.maxs[2] + 16);
    }

    /// The sites' spots, from the map's nodes (none inside: the box's middle).
    /// Only those another node's way leads to: not one atop a crate, gone
    /// down from but never up to (a bot sent there finds no way).
    inline void botMapOf(const LevelView &view, const ActorWorld &actors, BotMap &out)
    {
        out = BotMap();
        bool entered[256] = {};
        for (int l = 0; l < actors.linkCount; ++l)
            if (actors.links[l].to < 256)
                entered[actors.links[l].to] = true;
        for (std::uint32_t i = 0; i < view.logicCount && out.sites < kSitesMost; ++i)
        {
            const LogicRecord &r = view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::BombTarget))
                continue;
            const int s = out.sites++;
            out.record[s] = i;
            for (int n = 0; n < actors.nodeCount && out.spots[s] < kSiteSpots; ++n)
            {
                const PathNode &node = actors.nodes[n];
                if ((node.flags & (kPathNodeAir | kPathNodeWater)) || !inSiteBox(r, node.pos, 0) || (n < 256 && !entered[n]))
                    continue;
                for (int a = 0; a < 3; ++a)
                    out.spot[s][out.spots[s]][a] = node.pos[a];
                ++out.spots[s];
            }
            if (out.spots[s] == 0)
            {
                out.spot[s][0][0] = fxFromInt((r.mins[0] + r.maxs[0]) / 2);
                out.spot[s][0][1] = fxFromInt((r.mins[1] + r.maxs[1]) / 2);
                out.spot[s][0][2] = fxFromInt(r.mins[2]);
                out.spots[s] = 1;
            }
        }
    }

    /// A new round's bots: their minds cleared, the terrorists' site
    /// chosen, and -- with the player a counter-terrorist -- the bomb given
    /// to the first of the terrorists.
    inline void botsNewRound(const LevelView &view, const ActorWorld &actors, const BotRoster &bots,
                             std::uint8_t playerTeam, Round &r, const Setup &setup)
    {
        botMapOf(view, actors, brain.map);
        brain.view = &view;
        brain.skill = setup.botSkill;
        brain.paid = false;
        // The money and the guns: the game's first round, everyone's start;
        // after, what each has -- and, frozen, a rifle bought if it can be.
        const std::uint8_t sideTeam[2] = {static_cast<std::uint8_t>(playerTeam == kTeamT ? kTeamCT : kTeamT),
                                          playerTeam};
        brain.seed ^= static_cast<std::uint32_t>(r.number * 2654435761u + 0x9E3779B9u);
        // Where each team starts.
        if (!brain.homes)
        {
            const EntityRecord *t = nullptr, *ct = nullptr;
            if (teamStart(view, kTeamT, 0, t) && teamStart(view, kTeamCT, 0, ct))
            {
                for (int k = 0; k < 3; ++k)
                {
                    brain.tHome[k] = t->origin[k];
                    brain.ctHome[k] = ct->origin[k];
                }
                brain.homes = true;
            }
        }
        // Each team's money: short of a rifle and armour, on the average,
        // after a round lost (not the first, nor with the most a loss can
        // pay, when they buy what they can) -- they save.
        bool eco[2] = {false, false};
        for (int side = 0; side < 2; ++side)
        {
            const int count = bots.count[side];
            if (count <= 0 || r.number == 0)
                continue;
            int sum = 0;
            for (int n = 0; n < count; ++n)
                sum += brain.purse[side][n].money + (brain.purse[side][n].gun == botRifle(sideTeam[side]) ? 4000 : 0);
            const int need = kBotGunPrices[botRifle(sideTeam[side])] + 1000;
            const int losses = sideTeam[side] == kTeamT ? r.lossesT : r.lossesCT;
            eco[side] = sum / count < need && losses > 0 && losses < 4;
        }
        for (int side = 0; side < 2; ++side)
        {
            for (int n = 0; n < kBotSlotsPerSide; ++n)
            {
                BotPurse &p = brain.purse[side][n];
                if (r.number == 0)
                {
                    p.money = startMoney(setup);
                    p.gun = botPistol(sideTeam[side]);
                    p.armor = 0;
                }
                if (eco[side])
                    continue;
                const BotGun rifle = botRifle(sideTeam[side]);
                // (A counter-terrorist with no primary and more than 2500:
                // the shield instead, a third of the time -- a tenth, with
                // 4000 or more -- as the game's bots choose it.)
                if (sideTeam[side] == kTeamCT && p.gun == botPistol(kTeamCT) && p.money > 2500 &&
                    botRandom() % 1000 < (p.money < 4000 ? 333u : 100u))
                {
                    p.money -= kBotGunPrices[kGunShield];
                    p.gun = kGunShield;
                }
                if (p.gun != rifle && p.gun != kGunShield && p.money >= kBotGunPrices[rifle])
                {
                    p.money -= kBotGunPrices[rifle];
                    p.gun = rifle;
                }
                // ... then armour, with what is left: the helmet too, if it
                // can be had.
                if (p.armor < 2 && p.money >= 1000)
                {
                    p.money -= p.armor == 1 ? 350 : 1000;
                    p.armor = 2;
                }
                else if (p.armor == 0 && p.money >= 650)
                {
                    p.money -= 650;
                    p.armor = 1;
                }
                // ... then, a third of the time, a grenade, as the game's
                // bots buy one: a flashbang only on a side of bots alone
                // (not to blind a person on it), a smoke one time in ten,
                // else an HE.
                if (p.grenade == kGearNone && botRandom() % 1000 < 333)
                {
                    const int roll = static_cast<int>(botRandom() % 100);
                    const bool allBots = side == 0;
                    const std::uint8_t g = roll < 10 ? kGearSmoke : (allBots && roll < 35 ? kGearFlash : kGearHe);
                    const int price = g == kGearFlash ? 200 : 300;
                    if (p.money >= price)
                    {
                        p.money -= price;
                        p.grenade = g;
                    }
                }
            }
        }
        for (std::uint8_t &g : botGunHeld)
            g = 0;
        for (std::uint8_t &t : botTeamOf)
            t = 0;
        for (int side = 0; side < 2; ++side)
            for (int n = 0; n < bots.count[side]; ++n)
                if (bots.slot[side][n] >= 0 && bots.slot[side][n] < kBotGunActors)
                    botTeamOf[bots.slot[side][n]] = static_cast<std::uint8_t>(sideTeam[side] + 1);
        for (int side = 0; side < 2; ++side)
            for (int n = 0; n < bots.count[side]; ++n)
                if (bots.slot[side][n] >= 0 && bots.slot[side][n] < kBotGunActors)
                    botGunHeld[bots.slot[side][n]] = static_cast<std::uint8_t>(brain.purse[side][n].gun + 1);
        for (int side = 0; side < 2; ++side)
            for (int n = 0; n < kBotSlotsPerSide; ++n)
            {
                brain.mind[side][n] = BotMind();
                // (One down already is not shot down again.)
                if (n < bots.count[side] && bots.slot[side][n] >= 0)
                    brain.mind[side][n].wasUp = botUp(actors.states[bots.slot[side][n]]);
            }
        brain.site = brain.map.sites > 0 ? static_cast<int>(botRandom() % static_cast<std::uint32_t>(brain.map.sites)) : 0;
        clearFloorGuns();
        r.bombDropped = false;
        brain.fetcher = -1;
        // (What held them up last round may not now -- a body, a bot.)
        walkBlockedCount = 0;
        for (Noise &noise : ears.noise)
            noise.active = false;
        // The terrorists' play.
        {
            const int tSide = sideTeam[0] == kTeamT ? 0 : 1;
            TeamPlan &p = tPlan;
            p = TeamPlan();
            p.eco = eco[tSide];
            p.site = brain.site;
            p.fake = brain.map.sites > 1 ? 1 - p.site : p.site;
            const std::uint32_t roll = botRandom() % 100;
            p.play = p.eco ? Play::Rush
                           : (roll < 22 ? Play::Rush : (roll < 52 ? Play::Split : (roll < 84 ? Play::Default : Play::Fake)));
            if (brain.map.sites < 2 && p.play == Play::Fake)
                p.play = Play::Split;
            p.committed = p.play == Play::Rush;
            p.roll = botRandom();
            const int tCount = bots.count[tSide];
            p.lurker = (p.play == Play::Default || p.play == Play::Split) && tCount >= 3 && botRandom() % 2 == 0
                           ? tCount - 1
                           : -1;
            p.commitAtMs = 18000 + static_cast<int>(botRandom() % 14000);
            // (The players' orders are the round's: none yet, either side.)
            for (int t = 0; t < 2; ++t)
            {
                teamOrder[t] = Order::Auto;
                brain.orderSeen[t] = teamOrders[t];
                brain.trailCount[t] = 0;
            }
        }
        // The counter-terrorists' places: over the sites, the more at one
        // by the round; each site's first its anchor; maybe one forward.
        {
            const int ctSide = sideTeam[0] == kTeamCT ? 0 : 1;
            CtPlan &p = ctPlan;
            p = CtPlan();
            p.eco = eco[ctSide];
            const int count = bots.count[ctSide];
            const int sites = brain.map.sites > 0 ? brain.map.sites : 1;
            const int heavy = static_cast<int>(botRandom() % static_cast<std::uint32_t>(sites));
            bool anchored[kSitesMost] = {};
            for (int n = 0; n < count && n < kBotSlotsPerSide; ++n)
            {
                // (Turn about from the heavy site: 0 heavy, 1 other, 2
                // heavy, ...)
                const int s = sites > 1 ? (heavy + n) % sites : 0;
                p.site[n] = s;
                p.anchor[n] = !anchored[s];
                anchored[s] = true;
            }
            if (count >= 3 && botRandom() % 3 == 0)
                p.forward[count - 1] = true;
            // (A known map: one of its setups, the standard most often --
            // each one's site its place's, and each site's first its anchor.)
            const std::uint32_t roll = botRandom() % 10;
            p.setup = roll < 4 ? 0 : (roll < 6 ? 1 : (roll < 8 ? 2 : 3));
            if (brain.holds > 0)
            {
                bool held[kSitesMost] = {};
                for (int n = 0; n < count && n < kBotSlotsPerSide; ++n)
                {
                    const int h = brain.setup[p.setup][n];
                    if (h < 0)
                        continue;
                    p.site[n] = brain.holdSite[h];
                    p.anchor[n] = !held[p.site[n]] && !brain.holdEarly[h];
                    held[p.site[n]] = held[p.site[n]] || p.anchor[n];
                }
            }
        }
        r.botBomb = -1;
        if (r.bombMap && playerTeam != kTeamT && bots.count[0] > 0)
            r.botBomb = bots.slot[0][0];
    }

    inline fx flatApart(const fx *p, const fx *q)
    {
        const fx d[3] = {p[0] - q[0], p[1] - q[1], 0};
        return fxLength(d);
    }
    /// ... with a floor's height counted thrice (one over another is far).
    inline fx floorApart(const fx *p, const fx *q)
    {
        const fx dz = p[2] - q[2];
        return flatApart(p, q) + 3 * (dz < 0 ? -dz : dz);
    }

    /// Every node's distance along the links from node `from` (0xFFFF: no
    /// way), for a person.
    inline void nodeDistances(const ActorWorld &a, int from, std::uint16_t *far)
    {
        std::uint8_t done[kMaxPathNodes];
        for (int n = 0; n < a.nodeCount; ++n)
        {
            far[n] = 0xFFFF;
            done[n] = 0;
        }
        if (from < 0 || from >= a.nodeCount || a.links == nullptr)
            return;
        far[from] = 0;
        for (;;)
        {
            int at = -1;
            for (int n = 0; n < a.nodeCount; ++n)
                if (!done[n] && far[n] != 0xFFFF && (at < 0 || far[n] < far[at]))
                    at = n;
            if (at < 0)
                return;
            done[at] = 1;
            const PathNode &node = a.nodes[at];
            for (int l = node.firstLink; l < node.firstLink + node.linkCount && l < a.linkCount; ++l)
            {
                const PathLink &link = a.links[l];
                if (link.to >= a.nodeCount || (link.flags & kPathLinkHuman) == 0 || walkLinkBlocked(l))
                    continue;
                const int d = far[at] + link.length;
                if (d < far[link.to])
                    far[link.to] = static_cast<std::uint16_t>(d > 0xFFFE ? 0xFFFE : d);
            }
        }
    }

    // ------------------------------------------------------------ known maps
    //
    // A map whose ways are known as its players know them: each site's ways
    // in for the terrorists (a few points each, in turn -- each put on the
    // nearest node), and the counter-terrorists' places, in setups. Known by
    // where its sites are. Where a map is not, its ways are worked out
    // (makeVias).
    // (A point's height, where given -- not 0 -- tells floors apart: on
    // nuke, A is over B.)
    struct KnownRoute
    {
        std::int8_t site; ///< KnownMap::siteAt's
        std::uint8_t steps;
        std::int16_t at[kLaneSteps][3];
        bool told = false; ///< taken only when the radio says (listed after a site's others)
    };
    struct KnownHold
    {
        std::int8_t site;
        bool early; ///< only the round's first 30 s, nothing seen
        std::int16_t at[3];
        std::int16_t look[2];
    };
    struct KnownDuct
    {
        std::uint8_t count;
        std::int16_t at[6][3]; ///< the way in, ..., the way out
        std::int16_t pace = 110; ///< units a second: a crawl's; a stair's, a drop's faster
    };
    struct KnownMap
    {
        std::int16_t siteAt[2][3];
        KnownRoute routes[8];
        int routeCount;
        KnownHold holds[kHoldsMost];
        int holdCount;
        std::int8_t setups[kCtSetups][kBotSlotsPerSide]; ///< holds, by the bot's place in its team
        const char *wayNames[kRadioWaysMost]; ///< the radio's names for some of its routes ...
        std::int8_t wayRoutes[kRadioWaysMost]; ///< ... which
        int wayCount;
        KnownDuct ducts[2];
        int ductCount;
        const char *ctWayNames[kRadioWaysMost]; ///< the radio's names for places to hold ...
        std::int8_t ctWayHolds[kRadioWaysMost][2]; ///< ... which holds (-1: none)
        int ctWayCount;
    };
    // de_dust2: A (with its long, its short over the catwalk, and from mid
    // round by the counter-terrorists' start) and B (its upper tunnels, mid
    // through the double doors, the lower tunnels).
    inline constexpr KnownMap kDust2 = {
        {{1120, 2464}, {-1536, 2688}},
        {
            {0, 3, {{560, 350}, {1350, 700}, {1400, 1700}}},     // long A
            {0, 3, {{-150, 600}, {250, 1450}, {450, 2050}}},     // short A, the catwalk
            {0, 3, {{-150, 600}, {-415, 1550}, {-100, 2250}}},   // mid to A, by the CT start
            {1, 2, {{-1850, 900}, {-1950, 1450}, {0, 0}}},       // the upper tunnels
            {1, 3, {{-150, 600}, {-415, 1550}, {-750, 2300}}},   // mid to B, the double doors
            {1, 3, {{-150, 600}, {-1000, 1400, -111}, {-1950, 1450}}},  // the lower tunnels, up their stair to B
            // (Told only: from B's way in, the upper tunnels, down through
            // the lower to mid -- and on to A from there.)
            {0, 3, {{-1950, 1450}, {-1350, 1100, 32}, {-150, 600}}, true}, // tunnels to mid
        },
        7,
        {
            {0, false, {1150, 2450}, {1400, 1500}},  // 0 A site, on long
            {0, false, {1300, 2150}, {1400, 900}},   // 1 A, the long's end
            {0, false, {500, 2150}, {250, 1450}},    // 2 A short, on the catwalk
            {0, false, {-350, 2150}, {-415, 1300}},  // 3 the double doors, from the CT side
            {1, false, {-1500, 2650}, {-1950, 1650}}, // 4 B site, on the tunnels
            {1, false, {-900, 2450}, {-1700, 1800}}, // 5 B's doors
            {0, true, {650, 1050}, {560, 350}},      // 6 the long doors, early
            {1, true, {-1850, 1450}, {-1850, 800}},  // 7 the upper tunnels, early
        },
        8,
        {
            {0, 4, 2, 3, 5},  // the standard: two A, one mid, two B
            {0, 1, 2, 4, 3},  // a stack on A
            {4, 5, 0, 3, 7},  // a stack on B
            {6, 7, 3, 0, 4},  // aggressive: the long doors and the tunnels early
        },
        {"A SHORT", "MID DOORS", "B TUNNELS", "MID TO TUNNELS", "TUNNELS TO MID"},
        {1, 4, 3, 5, 6},
        5,
        // (The stair between the lower tunnels and the upper, too steep for
        // a bot to keep to: walked, up it and down it.)
        {{6, {{-1000, 1400, -111}, {-1060, 1290, -111}, {-1090, 1200, -79}, {-1140, 1130, -31}, {-1210, 1090, 0}, {-1350, 1100, 32}}, 200},
         {6, {{-1350, 1100, 32}, {-1210, 1090, 0}, {-1140, 1130, -31}, {-1090, 1200, -79}, {-1060, 1290, -111}, {-1000, 1400, -111}}, 200}},
        2,
        {"LONG A", "SHORT A", "MID", "B TUNNELS", "B DOORS"},
        {{1, 0}, {2, -1}, {3, -1}, {4, 7}, {5, -1}},
        5,
    };
    // de_nuke: A (upstairs: from outside, through its main doors -- the hut's
    // swinging door is no way for a bot: PathNodes.cpp) and B under it (down
    // the ramp; outside, down secret's stairs and along the hall; and A's
    // vent, crawled down).
    inline constexpr KnownMap kNuke = {
        {{680, -776, -415}, {648, -904, -767}},
        {
            {0, 3, {{-192, -1872, -416}, {720, -1968, -416}, {672, -1632, -416}}},  // 0 A: outside, the main doors
            {0, 3, {{-480, -1488, -416}, {384, -2160, -416}, {1104, -1632, -416}}}, // 1 A: outside, round by the far side
            {1, 3, {{-50, -450, -416}, {912, 528, -463}, {768, 48, -640}}},         // 2 B: the ramp
            {1, 3, {{720, -1968, -416}, {2208, -1968, -504}, {1536, -1300, -640}}}, // 3 B: outside, secret
            {1, 3, {{672, -1632, -416}, {690, -1350, -416}, {760, -1120, -768}}},   // 4 B: A's vent, down into it
        },
        5,
        {
            {0, false, {720, -1104, -416}, {336, -1296}},   // 0 A site, on the hut
            {0, false, {640, -940, -416}, {672, -1632}},    // 1 A, on the main doors
            {1, false, {672, -960, -768}, {720, -288}},     // 2 B site, on the ramp
            {1, false, {1152, -960, -768}, {1536, -1300}},  // 3 B, on the hall from secret
            {1, false, {960, 48, -416}, {300, 137}},        // 4 the ramp room, from the back
            {0, false, {1475, -825, -416}, {720, -1968}},   // 5 outside, from the far side
            {1, false, {1536, -1536, -640}, {1584, -2016}}, // 6 secret's foot
            {0, true, {1104, -1632, -416}, {-96, -2112}},   // 7 outside, early
        },
        8,
        {
            {0, 2, 4, 1, 3},  // the standard: two A, the ramp, two B
            {0, 1, 5, 2, 4},  // a stack on A, outside watched
            {2, 3, 6, 4, 0},  // a stack on B
            {7, 4, 0, 2, 6},  // aggressive: outside early
        },
        {"OUTSIDE SECRET", "VENT", "RAMP"},
        {3, 4, 2},
        3,
        // (A's vent: its grate at the foot of A's south wall, down the
        // shaft, along the duct, and out of its grate high in B's south
        // wall -- dropped down into B.)
        {{5, {{817, -1395, -415}, {816, -1400, -599}, {816, -1335, -599}, {816, -1300, -660}, {800, -1240, -768}}}},
        1,
        {"A SITE", "OUTSIDE", "RAMP", "SECRET"},
        {{0, 1}, {5, 7}, {4, 2}, {6, 3}},
        4,
    };
    // de_inferno: A to the east (by the apartments: up their stairs from
    // the back alley, along their upper hall, over the balcony and down its
    // stair; or through mid and under the arch) and B to the north (up the
    // banana). The counter-terrorists hold A from the balcony's and the
    // arch's sides and mid from theirs; B from the banana's end and from
    // their side of it -- the church.
    inline constexpr KnownMap kInferno = {
        {{1984, 456, 160}, {288, 2784, 160}},
        {
            {0, 3, {{-576, 432, 64}, {1575, -363, 272}, {2073, -200, 272}}},                  // 0 A: the apartments
            {0, 3, {{144, 624, 96}, {864, 480, 96}, {1440, 288, 160}}},                       // 1 A: mid, the arch
            {1, 3, {{96, 1152, 96}, {528, 1920, 128}, {768, 2352, 128}}},                      // 2 B: the banana
        },
        3,
        {
            {0, false, {2256, 384, 160}, {2073, -200}},   // 0 A site, on the balcony
            {0, false, {2256, 576, 160}, {1440, 288}},    // 1 A, on the arch
            {1, false, {480, 2688, 160}, {768, 2352}},    // 2 B site, on the banana's end
            {1, false, {1152, 2736, 128}, {768, 2544}},   // 3 B, from the church
            {0, false, {1824, 1152, 160}, {1056, 528}},   // 4 mid, from the counter-terrorists' side
            {1, true, {720, 2016, 128}, {144, 1440}},     // 5 the banana, early
            {0, false, {2400, 0, 160}, {1900, -188}},     // 6 A, deep, under the balcony
            {1, false, {336, 3024, 160}, {768, 2352}},    // 7 B, the back of the site
        },
        8,
        {
            {0, 1, 4, 2, 3},  // the standard: two A, mid, two B
            {0, 1, 6, 4, 2},  // a stack on A
            {2, 3, 7, 5, 0},  // a stack on B
            {5, 4, 0, 3, 2},  // aggressive: the banana early
        },
        {"APPS", "MID", "BANANA"},
        {0, 1, 2},
        3,
        // (The apartments' stair, too narrow and steep for the nodes laid
        // over the floors to go up, walked up; and the balcony's down to A.)
        {{5, {{-576, 432, 64}, {-560, 350, 64}, {-500, 350, 64}, {-250, 350, 192}, {-200, 323, 192}}, 200},
         {6, {{2073, -200, 272}, {2040, -170, 272}, {2040, -95, 256}, {2085, -95, 208}, {2090, -60, 160}, {2090, 0, 160}}, 200}},
        2,
        {"BANANA", "CHURCH", "APPS", "MID"},
        {{5, 2}, {3, 7}, {0, 6}, {4, 1}},
        4,
    };

    /// `known` put on the map: its sites matched to the map's, its points
    /// on its nodes. False if this is not the map.
    inline bool applyKnownMap(const ActorWorld &a, const KnownMap &known)
    {
        if (brain.map.sites < 2)
            return false;
        int siteOf[2] = {-1, -1};
        for (int k = 0; k < 2; ++k)
        {
            const fx at[3] = {fxFromInt(known.siteAt[k][0]), fxFromInt(known.siteAt[k][1]), fxFromInt(known.siteAt[k][2])};
            for (int s = 0; s < brain.map.sites; ++s)
                for (int j = 0; j < brain.map.spots[s]; ++j)
                {
                    const fx dz = brain.map.spot[s][j][2] - at[2];
                    if (flatApart(at, brain.map.spot[s][j]) < fxFromInt(500) &&
                        (known.siteAt[k][2] == 0 || (dz < fxFromInt(160) && dz > -fxFromInt(160))))
                        siteOf[k] = s;
                }
        }
        if (siteOf[0] < 0 || siteOf[1] < 0 || siteOf[0] == siteOf[1])
            return false;
        brain.siteA = siteOf[0];
        brain.siteB = siteOf[1];
        // (A point: on the node nearest it, as the crow flies -- of its
        // floor, given its height.)
        auto onNode = [&](const std::int16_t *xy, fx *out)
        {
            int best = -1;
            fx bestFar = 0;
            const fx at[3] = {fxFromInt(xy[0]), fxFromInt(xy[1]), fxFromInt(xy[2])};
            for (int v = 0; v < a.nodeCount; ++v)
            {
                const fx dz = a.nodes[v].pos[2] - at[2];
                const fx far = flatApart(at, a.nodes[v].pos) + (xy[2] == 0 ? 0 : 3 * (dz < 0 ? -dz : dz));
                if (best < 0 || far < bestFar)
                {
                    best = v;
                    bestFar = far;
                }
            }
            if (best < 0)
                return false;
            for (int k = 0; k < 3; ++k)
                out[k] = a.nodes[best].pos[k];
            return true;
        };
        for (int s = 0; s < kSitesMost; ++s)
        {
            brain.lanes[s] = 0;
            brain.shortestToo[s] = false;
        }
        for (int i = 0; i < known.routeCount && i < 8; ++i)
        {
            const KnownRoute &route = known.routes[i];
            const int s = siteOf[route.site];
            brain.routeSite[i] = static_cast<std::int8_t>(s);
            brain.routeLane[i] = -1;
            if (brain.lanes[s] >= kLanesMost)
                continue;
            const int lane = brain.lanes[s];
            int steps = 0;
            for (int k = 0; k < route.steps && k < kLaneSteps; ++k)
                steps += onNode(route.at[k], brain.lane[s][lane][steps]) ? 1 : 0;
            if (steps == 0)
                continue;
            brain.laneSteps[s][lane] = steps;
            brain.routeLane[i] = static_cast<std::int8_t>(lane);
            ++brain.lanes[s];
            if (!route.told)
                brain.autoLanes[s] = brain.lanes[s];
        }
        // (Its ways the radio names, and its ducts: as they are.)
        radioWays = 0;
        for (int k = 0; k < known.wayCount && k < kRadioWaysMost; ++k)
        {
            const int route = known.wayRoutes[k];
            if (route < 0 || route >= known.routeCount || brain.routeLane[route] < 0)
                continue;
            radioWayName[radioWays] = known.wayNames[k];
            brain.wayRoute[radioWays] = static_cast<std::int8_t>(route);
            ++radioWays;
        }
        brain.ducts = 0;
        for (int d = 0; d < known.ductCount && d < 2; ++d)
        {
            const KnownDuct &duct = known.ducts[d];
            for (int k = 0; k < duct.count && k < 6; ++k)
                for (int c = 0; c < 3; ++c)
                    brain.duct[d][k][c] = fxFromInt(duct.at[k][c]);
            brain.ductLen[d] = duct.count < 6 ? duct.count : 6;
            brain.ductPace[d] = duct.pace > 0 ? duct.pace : 110;
            ++brain.ducts;
        }
        brain.holds = 0;
        for (int i = 0; i < known.holdCount && brain.holds < kHoldsMost; ++i)
        {
            const KnownHold &hold = known.holds[i];
            const int h = brain.holds;
            if (!onNode(hold.at, brain.hold[h]))
                continue;
            brain.holdLook[h][0] = fxFromInt(hold.look[0]);
            brain.holdLook[h][1] = fxFromInt(hold.look[1]);
            brain.holdLook[h][2] = brain.hold[h][2] + fxFromInt(48);
            brain.holdSite[h] = siteOf[hold.site];
            brain.holdEarly[h] = hold.early;
            ++brain.holds;
        }
        // (Its setups, as holds; a hold left out, the site's spot instead.)
        for (int k = 0; k < kCtSetups; ++k)
            for (int n = 0; n < kBotSlotsPerSide; ++n)
                brain.setup[k][n] = known.setups[k][n] < brain.holds ? known.setups[k][n] : -1;
        // (The counter-terrorists' places the radio names: their holds.)
        ctWays = 0;
        for (int k = 0; k < known.ctWayCount && k < kRadioWaysMost; ++k)
        {
            const std::int8_t *holds = known.ctWayHolds[k];
            if (holds[0] < 0 || holds[0] >= brain.holds)
                continue;
            ctWayName[ctWays] = known.ctWayNames[k];
            brain.ctWayHold[ctWays][0] = holds[0];
            brain.ctWayHold[ctWays][1] = holds[1] >= 0 && holds[1] < brain.holds ? holds[1] : holds[0];
            ++ctWays;
        }
        return true;
    }

    /// BotBrain::via, from `spawn` (where the terrorists start).
    inline void makeVias(const ActorWorld &a, const MoveWorld &world, const fx *spawn)
    {
        brain.viaMade = true;
        const int start = pathNearest(a, world, spawn);
        if (start < 0)
            return;
        std::uint16_t fromSpawn[kMaxPathNodes], fromSite[kMaxPathNodes], fromCt[kMaxPathNodes];
        nodeDistances(a, start, fromSpawn);
        // (And from the counter-terrorists' start: a place to gather, or the
        // way round, is to be on the terrorists' side of the site -- not
        // through the other side's start, however short that way is.)
        const int ctStart = brain.homes ? pathNearest(a, world, brain.ctHome) : -1;
        nodeDistances(a, ctStart, fromCt);
        if (ctStart < 0)
            for (int v = 0; v < a.nodeCount; ++v)
                fromCt[v] = 0xFFFE;
        for (int s = 0; s < brain.map.sites; ++s)
        {
            brain.viaOk[s] = false;
            if (brain.map.spots[s] <= 0)
                continue;
            const int goal = pathNearest(a, world, brain.map.spot[s][0]);
            if (goal < 0 || fromSpawn[goal] == 0xFFFF)
                continue;
            nodeDistances(a, goal, fromSite);
            const int best = fromSpawn[goal];
            const int ctToSite = fromCt[goal] == 0xFFFF ? 0 : fromCt[goal];
#ifdef CSBOTS_DEBUG_PRINT
            std::printf("  makeVias site %d: start %d ct %d goal %d best %d ctToSite %d\n", s, start, ctStart, goal, best,
                        ctToSite);
#endif
            // (The links' lengths run well over the straight line, round
            // corners and up stairs: what is near is reckoned against the
            // whole way's length.)
            auto theirSide = [&](int v) { return fromCt[v] == 0xFFFE || fromCt[v] >= ctToSite + best / 10; };
            // (The gathering place: on the shortest way, some 900 units
            // short of the site as the crow flies, on their side of it.)
            {
                int stage = -1;
                fx off = 0;
                const fx *site = brain.map.spot[s][0];
                for (int v = 0; v < a.nodeCount; ++v)
                {
                    if (fromSpawn[v] == 0xFFFF || fromSite[v] == 0xFFFF || fromSpawn[v] + fromSite[v] > best + 64 ||
                        !theirSide(v))
                        continue;
                    const fx far = flatApart(a.nodes[v].pos, site);
                    const fx miss = far > fxFromInt(900) ? far - fxFromInt(900) : fxFromInt(900) - far;
                    if (far < fxFromInt(500) || far > fxFromInt(1900))
                        continue;
                    if (stage < 0 || miss < off)
                    {
                        stage = v;
                        off = miss;
                    }
                }
                brain.stageOk[s] = stage >= 0;
                for (int k = 0; k < 3 && stage >= 0; ++k)
                    brain.stage[s][k] = a.nodes[stage].pos[k];
            }
            // The other ways in (lanes): each through a node as far as can
            // be from the ways already found -- the shortest first -- on a
            // way not much longer; and its corridor marked, for the next.
            std::uint8_t taken[kMaxPathNodes];
            for (int v = 0; v < a.nodeCount; ++v)
                taken[v] = fromSpawn[v] != 0xFFFF && fromSite[v] != 0xFFFF && fromSpawn[v] + fromSite[v] <= best + 48;
            brain.lanes[s] = 0;
            // By the map's named places, where its bots' navigation gave
            // them (HlLevel.hpp's kPathNodePlaceShift): each a way in of its
            // own -- through the place's node on the shortest way through it
            // -- but the starts', the site's own, and those the shortest way
            // in goes through already (lane 0).
            {
                bool named = false;
                for (int v = 0; v < a.nodeCount && !named; ++v)
                    named = pathNodePlace(a.nodes[v]) != 0;
                if (named)
                {
                    bool skip[32] = {};
                    skip[0] = true;
                    skip[pathNodePlace(a.nodes[start])] = true;
                    if (ctStart >= 0)
                        skip[pathNodePlace(a.nodes[ctStart])] = true;
                    skip[pathNodePlace(a.nodes[goal])] = true;
                    for (int v = 0; v < a.nodeCount; ++v)
                        if (taken[v] && fromSpawn[v] + fromSite[v] <= best + 48 && fromSite[v] > best / 3)
                            skip[pathNodePlace(a.nodes[v])] = true;
                    // Each place's node: the one furthest off the shortest
                    // way (its far end -- the upper tunnels, not where they
                    // come out by it), on a way not much longer.
                    int through[32], node[32];
                    fx score[32];
                    for (int p = 0; p < 32; ++p)
                        node[p] = -1;
                    for (int v = 0; v < a.nodeCount; ++v)
                    {
                        const int p = pathNodePlace(a.nodes[v]);
                        if (skip[p] || fromSpawn[v] == 0xFFFF || fromSite[v] == 0xFFFF || !theirSide(v))
                            continue;
                        const int length = fromSpawn[v] + fromSite[v];
                        if (length > best * 15 / 10 || fromSpawn[v] < best / 5)
                            continue;
                        fx apart = -1;
                        for (int c = 0; c < a.nodeCount; ++c)
                        {
                            if (!taken[c])
                                continue;
                            const fx far = flatApart(a.nodes[c].pos, a.nodes[v].pos);
                            if (apart < 0 || far < apart)
                                apart = far;
                        }
                        if (apart < fxFromInt(300))
                            continue;
                        // (A way to this site, not round by the other: nearer
                        // this one than any other site.)
                        bool ours = true;
                        for (int o = 0; o < brain.map.sites && ours; ++o)
                            ours = o == s || brain.map.spots[o] <= 0 ||
                                   flatApart(a.nodes[v].pos, brain.map.spot[s][0]) <
                                       flatApart(a.nodes[v].pos, brain.map.spot[o][0]);
                        if (!ours)
                            continue;
                        const fx worth = static_cast<fx>(static_cast<std::int64_t>(apart) * best / length);
                        if (node[p] < 0 || worth > score[p])
                        {
                            node[p] = v;
                            through[p] = length;
                            score[p] = worth;
                        }
                    }
                    (void)through;
                    while (brain.lanes[s] < kLanesMost)
                    {
                        int pick = -1;
                        for (int p = 1; p < 32; ++p)
                            if (node[p] >= 0 && (pick < 0 || score[p] > score[pick]))
                                pick = p;
                        if (pick < 0)
                            break;
                        for (int k = 0; k < 3; ++k)
                            brain.lane[s][brain.lanes[s]][0][k] = a.nodes[node[pick]].pos[k];
                        brain.laneSteps[s][brain.lanes[s]] = 1;
                        ++brain.lanes[s];
                        node[pick] = -1;
                    }
                }
            }
            for (int lane = brain.lanes[s]; lane < kLanesMost && brain.lanes[s] == 0; ++lane)
            {
                int pick = -1;
                fx pickApart = 0;
                for (int v = 0; v < a.nodeCount; ++v)
                {
                    if (fromSpawn[v] == 0xFFFF || fromSite[v] == 0xFFFF || taken[v])
                        continue;
                    const int through = fromSpawn[v] + fromSite[v];
                    if (through > best * 22 / 10 || fromSpawn[v] < best * 3 / 10 ||
                        fromSite[v] < best * 2 / 10 || !theirSide(v))
                        continue;
                    // (On the way: nearer the site than the start is, well.)
                    if (flatApart(a.nodes[v].pos, brain.map.spot[s][0]) * 20 > flatApart(spawn, brain.map.spot[s][0]) * 13)
                        continue;
                    fx apart = -1;
                    for (int c = 0; c < a.nodeCount; ++c)
                    {
                        if (!taken[c])
                            continue;
                        const fx far = flatApart(a.nodes[c].pos, a.nodes[v].pos);
                        if (apart < 0 || far < apart)
                            apart = far;
                    }
                    // (Apart, for its length: a way much longer counts for less.)
                    apart = apart >= fxFromInt(450) ? static_cast<fx>(static_cast<std::int64_t>(apart) * best / through) : -1;
                    if (apart > pickApart)
                    {
                        pick = v;
                        pickApart = apart;
                    }
                }
                if (pick < 0)
                    break;
                for (int k = 0; k < 3; ++k)
                    brain.lane[s][lane][0][k] = a.nodes[pick].pos[k];
                brain.laneSteps[s][lane] = 1;
                ++brain.lanes[s];
                // (Its corridor: from the spawn to it, and on to the site.)
                std::uint16_t fromPick[kMaxPathNodes];
                nodeDistances(a, pick, fromPick);
                for (int v = 0; v < a.nodeCount; ++v)
                {
                    if (fromPick[v] == 0xFFFF)
                        continue;
                    if ((fromSpawn[v] != 0xFFFF && fromSpawn[v] + fromPick[v] <= fromSpawn[pick] + 48) ||
                        (fromSite[v] != 0xFFFF && fromPick[v] + fromSite[v] <= fromSite[pick] + 48))
                        taken[v] = 1;
                }
            }
            brain.viaOk[s] = brain.lanes[s] > 0;
            brain.autoLanes[s] = brain.lanes[s];
            brain.shortestToo[s] = true;
            for (int k = 0; k < 3 && brain.viaOk[s]; ++k)
                brain.via[s][k] = brain.lane[s][0][0][k];
        }
        // (A known map: its own ways, and its counter-terrorists' places.)
        brain.holds = 0;
        brain.siteA = 0;
        brain.siteB = brain.map.sites > 1 ? 1 : 0;
        brain.ducts = 0;
        radioWays = 0;
        ctWays = 0;
        if (!applyKnownMap(a, kDust2) && !applyKnownMap(a, kNuke))
            applyKnownMap(a, kInferno);
    }

    /// What the bots did to the round this frame.
    struct BotOut
    {
        bool planted = false;
        bool defused = false; ///< (and the round is over: endRound)
        bool defusing = false;
    };


    /// A node near the bot it can walk to from where it is, as itself, not
    /// the one it is stuck going to: picked by turns, so that each try is
    /// another. False if there is none.
    inline bool botDetour(const ActorWorld &a, const MoveWorld &world, int index, int turn, fx *out)
    {
        const ActorState &bot = a.states[index];
        int found = 0;
        int pick = -1;
        for (int pass = 0; pass < 2 && pick < 0; ++pass)
        {
            for (int n = 0; n < a.nodeCount; ++n)
            {
                const PathNode &node = a.nodes[n];
                if (n == bot.goalNode || (node.flags & (kPathNodeAir | kPathNodeWater)))
                    continue;
                const fx d[3] = {node.pos[0] - bot.pos[0], node.pos[1] - bot.pos[1], node.pos[2] - bot.pos[2]};
                if (d[0] > fxFromInt(400) || d[0] < -fxFromInt(400) || d[1] > fxFromInt(400) || d[1] < -fxFromInt(400) ||
                    d[2] > fxFromInt(64) || d[2] < -fxFromInt(64))
                    continue;
                const fx flat[3] = {d[0], d[1], 0};
                if (fxLength(flat) < fxFromInt(48) ||
                    !walkClear(world, &a, index, kWalkStanding, bot.pos, node.pos, 28))
                    continue;
                // (First pass: count them; second: take the turn's.)
                if (pass == 0)
                    ++found;
                else
                {
                    const int here = found--;
                    if (here == 1 + turn % (here + 1))
                        pick = n;
                }
            }
            if (found == 0)
                return false;
            if (pass == 0)
            {
                const int total = found;
                found = 0;
                for (int n = 0; n < a.nodeCount && pick < 0; ++n)
                {
                    const PathNode &node = a.nodes[n];
                    if (n == bot.goalNode || (node.flags & (kPathNodeAir | kPathNodeWater)))
                        continue;
                    const fx d[3] = {node.pos[0] - bot.pos[0], node.pos[1] - bot.pos[1], node.pos[2] - bot.pos[2]};
                    if (d[0] > fxFromInt(400) || d[0] < -fxFromInt(400) || d[1] > fxFromInt(400) ||
                        d[1] < -fxFromInt(400) || d[2] > fxFromInt(64) || d[2] < -fxFromInt(64))
                        continue;
                    const fx flat[3] = {d[0], d[1], 0};
                    if (fxLength(flat) < fxFromInt(48) ||
                        !walkClear(world, &a, index, kWalkStanding, bot.pos, node.pos, 28))
                        continue;
                    if (found++ == turn % total)
                        pick = n;
                }
                break;
            }
        }
        if (pick < 0)
            return false;
        for (int k = 0; k < 3; ++k)
            out[k] = a.nodes[pick].pos[k];
        return true;
    }

    /// A bot's sounds, as a player's: its steps at a run (by the floor, as
    /// the player's are -- a walk is silent, as in Counter-Strike), and its
    /// death.
    inline void botSounds(LogicWorld &w, const LevelView &view, const ActorState &bot, BotMind &mind, int ms,
                          int index, std::uint8_t team)
    {
        if (w.heard == nullptr)
            return;
        const bool up = botUp(bot);
        if (!up && mind.wasUp && bot.active)
        {
            static int which = 0;
            const CsSound die = static_cast<CsSound>(static_cast<int>(CsSound::Die1) + (which++ % 3));
            const std::uint16_t sound = findSound(view, die);
            if (sound != kNoSound)
                w.heard->add(sound, bot.pos, 127, kRadiusMedium);
        }
        mind.wasUp = up;
        if (!up)
        {
            mind.stepKnown = false;
            return;
        }
        const fx moved[3] = {bot.pos[0] - mind.stepFrom[0], bot.pos[1] - mind.stepFrom[1], 0};
        const fx far = fxLength(moved);
        for (int k = 0; k < 3; ++k)
            mind.stepFrom[k] = bot.pos[k];
        if (!mind.stepKnown || ms <= 0)
        {
            mind.stepKnown = true;
            return;
        }
        if (mind.stepLeft > 0)
            mind.stepLeft -= ms;
        // (At a run: over 150 units a second.)
        if (far * 1000 < fxFromInt(150) * ms || mind.stepLeft > 0)
            return;
        mind.stepLeft = 300;
        hearNoise(bot.pos, index, team, 0);
        CommonSound first = CommonSound::Step1;
        switch (floorMaterial(view, bot.pos, -1))
        {
        case Material::Metal: first = CommonSound::Metal1; break;
        case Material::Dirt: first = CommonSound::Dirt1; break;
        case Material::Vent: first = CommonSound::Duct1; break;
        case Material::Grate: first = CommonSound::Grate1; break;
        case Material::Tile: first = CommonSound::Tile1; break;
        case Material::Slosh: first = CommonSound::Slosh1; break;
        default: break;
        }
        if (static_cast<std::uint32_t>(first) >= view.soundCount || !view.soundThere(static_cast<int>(first)))
            first = CommonSound::Step1;
        static unsigned step = 0;
        w.heard->add(static_cast<std::uint16_t>(static_cast<int>(first) + (step++ & 3)), bot.pos, 64, kRadiusMedium);
    }

    /// A kill for `killer` (a bot's actor, or kAiPlayer) of one of team
    /// `victimTeam`: none for one's own side, as Counter-Strike takes one
    /// away for that -- here it is only not counted.
    inline void creditKill(const BotRoster &bots, const std::uint8_t *sideTeam, int killer, std::uint8_t victimTeam)
    {
        if (killer == kAiPlayer)
        {
            if (victimTeam != sideTeam[1])
                ++scores.player.kills;
            return;
        }
        for (int side = 0; side < 2; ++side)
        {
            for (int n = 0; n < bots.count[side]; ++n)
            {
                if (bots.slot[side][n] == killer && sideTeam[side] != victimTeam)
                    ++scores.bot[side][n].kills;
            }
        }
    }
    inline void scoreDeath(const ActorWorld &a, const BotRoster &bots, const std::uint8_t *sideTeam, int side, int n,
                           int index)
    {
        ++scores.bot[side][n].deaths;
        if (index < a.count)
            hearNoise(a.states[index].pos, index, sideTeam[side], 4);
        // (Its gun where it fell.)
        const int gun = botGunOf(index);
        // (The shield's pistol: the USP. The shield is not left lying.)
        if (gun < kBotGuns && index < a.count)
            dropGun(a.states[index].pos, itemNamed(kFloorGunItems[gun == kGunShield ? kGunUsp : gun]));
        creditKill(bots, sideTeam, index < 256 ? scores.hitBy[index] : -1, sideTeam[side]);
        if (index < 256)
            scores.hitBy[index] = -1;
    }

    /// The site `at` is by (within `reach`), or -1: by its spots and its
    /// gathering place.
    inline int siteNear(const fx *at, int reach)
    {
        int best = -1;
        fx bestFar = fxFromInt(reach);
        for (int s = 0; s < brain.map.sites; ++s)
        {
            for (int k = 0; k < brain.map.spots[s]; ++k)
            {
                const fx far = flatApart(at, brain.map.spot[s][k]);
                if (far < bestFar)
                {
                    best = s;
                    bestFar = far;
                }
            }
            if (brain.stageOk[s] && flatApart(at, brain.stage[s]) < bestFar)
            {
                best = s;
                bestFar = flatApart(at, brain.stage[s]);
            }
        }
        return best;
    }

    /// An enemy seen at `at` by one of `team`: for the counter-terrorists,
    /// its site called; for the terrorists, counted against it.
    inline void seenAt(const fx *at, std::uint8_t team, const Round &r)
    {
        const int s = siteNear(at, 1500);
        if (s < 0)
            return;
        if (team == kTeamCT)
        {
            ctPlan.called = s;
            ctPlan.calledMs = r.liveMs;
        }
        else if (tPlan.seen[s] < 1000)
            ++tPlan.seen[s];
    }

    /// The frame's noises: the old fade; the player's shots, running feet
    /// and planting; the bots' shots. The terrorists' heard at a site call
    /// it, for the counter-terrorists.
    inline void botsListen(const ActorWorld &a, const PlayerState &player, const BotRoster &bots,
                           const std::uint8_t *sideTeam, std::uint8_t playerTeam, const Round &r, int ms)
    {
        for (Noise &noise : ears.noise)
        {
            if (noise.active && (noise.ageMs += ms) > kNoiseMs * 2)
                noise.active = false;
        }
        if (ears.playerFired && player.health > 0)
            hearNoise(player.origin, kAiPlayer, playerTeam, 1);
        ears.playerFired = false;
        if (player.health > 0 && player.onGround)
        {
            const fx run[3] = {player.velocity[0], player.velocity[1], 0};
            const fx feet[3] = {player.origin[0], player.origin[1], player.origin[2] - fxFromInt(36)};
            if (fxLength(run) > fxFromInt(150))
                hearNoise(feet, kAiPlayer, playerTeam, 0);
        }
        if (r.plantMs > 0 && player.health > 0)
            hearNoise(player.origin, kAiPlayer, playerTeam, 2);
        for (int side = 0; side < 2; ++side)
        {
            for (int n = 0; n < bots.count[side]; ++n)
            {
                const int index = bots.slot[side][n];
                if (index < 0 || index >= kBotGunActors || !ears.botFired[index])
                    continue;
                ears.botFired[index] = 0;
                hearNoise(a.states[index].pos, index, sideTeam[side], 1);
            }
        }
        for (const Noise &noise : ears.noise)
        {
            if (!noise.active || noise.ageMs != 0 || noise.team != kTeamT || noise.loud > 2)
                continue;
            const int s = siteNear(noise.pos, noise.loud == 0 ? 700 : 1000);
            if (s >= 0)
            {
                ctPlan.called = s;
                ctPlan.calledMs = r.liveMs;
            }
        }
    }

    /// The counter-terrorists' retake: where they gather, short of the bomb
    /// on their own side, and when they go in -- two there, or the most
    /// that are left, or the bomb near going off.
    inline void retakeTick(const ActorWorld &a, const MoveWorld &world, const BotRoster &bots,
                           const std::uint8_t *sideTeam, const Round &r)
    {
        CtPlan &p = ctPlan;
        if (p.retakeGo)
            return;
        const int ctSide = sideTeam[0] == kTeamCT ? 0 : 1;
        if (!p.retakeMade)
        {
            p.retakeMade = true;
            if (!brain.homes)
            {
                p.retakeGo = true;
                return;
            }
            // (700 units from the bomb toward their start: the node nearest.)
            fx toward[3] = {brain.ctHome[0] - r.bomb[0], brain.ctHome[1] - r.bomb[1], 0};
            const fx far = fxLength(toward);
            if (far < fxFromInt(900))
            {
                p.retakeGo = true;
                return;
            }
            fx at[3];
            for (int k = 0; k < 2; ++k)
                at[k] = r.bomb[k] + static_cast<fx>(static_cast<std::int64_t>(toward[k]) * fxFromInt(700) / far);
            at[2] = r.bomb[2];
            const int node = pathNearest(a, world, at);
            if (node < 0)
            {
                p.retakeGo = true;
                return;
            }
            for (int k = 0; k < 3; ++k)
                p.retake[k] = a.nodes[node].pos[k];
        }
        int alive = 0, there = 0;
        for (int n = 0; n < bots.count[ctSide]; ++n)
        {
            const ActorState &bot = a.states[bots.slot[ctSide][n]];
            if (!botUp(bot))
                continue;
            ++alive;
            const fx feet[3] = {r.bomb[0], r.bomb[1], r.bomb[2] - fxFromInt(36)};
            there += flatApart(bot.pos, p.retake) < fxFromInt(260) || flatApart(bot.pos, feet) < fxFromInt(500) ? 1 : 0;
        }
        if (alive <= 1 || there >= 2 || there >= alive || r.bombMs < 22000)
            p.retakeGo = true;
    }

    /// The terrorists' play, as a team: when they go in, and where.
    inline void playsTick(const ActorWorld &a, const BotRoster &bots, const std::uint8_t *sideTeam, const Round &r,
                          int ms)
    {
        TeamPlan &p = tPlan;
        if (p.committed)
            return;
        const int tSide = sideTeam[0] == kTeamT ? 0 : 1;
        int alive = 0, gathered = 0;
        for (int n = 0; n < bots.count[tSide]; ++n)
        {
            if (!botUp(a.states[bots.slot[tSide][n]]))
                continue;
            ++alive;
            gathered += brain.mind[tSide][n].atStage ? 1 : 0;
        }
        // (One alone has nobody to play with: in at once.)
        bool go = alive <= 1 || r.clockMs < 35000 || r.liveMs > 50000;
        switch (p.play)
        {
        case Play::Split:
            if (gathered > 0)
                p.holdMs += ms;
            go = go || gathered >= alive || p.holdMs > 9000;
            break;
        case Play::Default:
            go = go || r.liveMs >= p.commitAtMs;
            // (In where they were seen least.)
            if (go && brain.map.sites > 1)
                p.site = p.seen[0] <= p.seen[1] ? (p.seen[0] == p.seen[1] ? p.site : 0) : 1;
            break;
        case Play::Fake:
            if (gathered * 2 >= alive && gathered > 0)
                p.holdMs += ms;
            go = go || p.holdMs > 6000;
            break;
        case Play::Rush: go = true; break;
        }
        if (!go)
            return;
        p.committed = true;
        brain.site = p.site;
        for (int n = 0; n < kBotSlotsPerSide; ++n)
            brain.mind[tSide][n].atStage = false;
    }

    /// The orders each team's bots are under (HlCs.hpp: teamOrder), put
    /// to them as they are given: the terrorists' a play of theirs
    /// (TeamPlan); the counter-terrorists' their places (CtPlan); both
    /// sides' follow, hold -- an errand each, till the next (errandOf).
    /// AUTO leaves them to their own play.
    inline void ordersTick(const ActorWorld &a, const BotRoster &bots, const std::uint8_t *sideTeam,
                           std::uint8_t playerTeam, const PlayerState &player, const Round &r)
    {
        for (int t = 0; t < 2; ++t)
        {
            const std::uint8_t team = t == 0 ? kTeamT : kTeamCT;
            const int side = sideTeam[0] == team ? 0 : 1;
            // (Whoever gave the order: where they are, if they are up -- this
            // console's player, or another's in a match.)
            fx feet[3] = {0, 0, 0};
            const int leader = teamLeader[t];
            bool up = false;
            if (leader == kAiPlayer)
            {
                up = playerTeam == team && player.health > 0;
                for (int k = 0; k < 3; ++k)
                    feet[k] = player.origin[k] - (k == 2 ? fxFromInt(36) : 0);
            }
            else if (leader >= 0 && leader < a.count)
            {
                const ActorState &them = a.states[leader];
                up = them.active && them.ai < kAiDying;
                for (int k = 0; k < 3; ++k)
                    feet[k] = them.pos[k] - (k == 2 ? fxFromInt(36) : 0);
            }
            brain.leaderUp[t] = up;
            // (Their way, for the bots to follow: a point each 80 units.)
            auto crumb = [&]()
            {
                const int last = kBotSlotsPerSide;
                for (int i = 0; i < last; ++i)
                    for (int k = 0; k < 3; ++k)
                        brain.trail[t][i][k] = brain.trail[t][i + 1][k];
                for (int k = 0; k < 3; ++k)
                    brain.trail[t][last][k] = feet[k];
                brain.trailCount[t] = brain.trailCount[t] < last + 1 ? brain.trailCount[t] + 1 : last + 1;
            };
            if (up && (brain.trailCount[t] == 0 || flatApart(feet, brain.trail[t][kBotSlotsPerSide]) > fxFromInt(80)))
                crumb();
            if (brain.orderSeen[t] == teamOrders[t])
                continue;
            brain.orderSeen[t] = teamOrders[t];
            if (brain.map.sites <= 0)
                continue;
            auto siteOk = [](int s) { return s >= 0 && s < brain.map.sites ? s : 0; };
            const Order order = teamOrder[t];
            // Both sides': where each was when told to hold; the way to follow.
            if (order == Order::Hold)
                for (int n = 0; n < bots.count[side] && n < kBotSlotsPerSide; ++n)
                    for (int k = 0; k < 3; ++k)
                        brain.held[t][n][k] = a.states[bots.slot[side][n]].pos[k];
            if (order == Order::FollowMe)
            {
                brain.trailCount[t] = 0;
                if (up)
                    crumb();
            }
            if (team == kTeamCT)
            {
                CtPlan &p = ctPlan;
                p.told = -1;
                p.push = false;
                p.forRetake = false;
                p.wayHold = -1;
                switch (order)
                {
                case Order::GoA:
                case Order::GoB:
                    // (The bomb is going there: to it.)
                    p.told = siteOk(order == Order::GoA ? brain.siteA : brain.siteB);
                    p.called = p.told;
                    p.calledMs = r.liveMs;
                    break;
                case Order::Rush:
                    p.push = true;
                    break;
                case Order::Split:
                    // (The standard again: what was seen forgotten.)
                    p.setup = 0;
                    p.called = -1;
                    break;
                case Order::Default:
                    p.forRetake = true;
                    // (Down already: in together, now.)
                    if (r.phase == Phase::Planted)
                        p.retakeGo = true;
                    break;
                case Order::Way0:
                case Order::Way1:
                case Order::Way2:
                case Order::Way3:
                case Order::Way4:
                {
                    const int k = static_cast<int>(order) - static_cast<int>(Order::Way0);
                    if (k < ctWays)
                        p.wayHold = k;
                    break;
                }
                default: break;
                }
            }
            else
            {
                TeamPlan &p = tPlan;
                p.lane = -1;
                switch (order)
                {
                case Order::Way0:
                case Order::Way1:
                case Order::Way2:
                case Order::Way3:
                case Order::Way4:
                {
                    // (One of the map's ways, all of them by it, now.)
                    const int k = static_cast<int>(order) - static_cast<int>(Order::Way0);
                    const int route = k < radioWays ? brain.wayRoute[k] : -1;
                    if (route >= 0 && route < 8 && brain.routeLane[route] >= 0)
                    {
                        p.site = siteOk(brain.routeSite[route]);
                        p.lane = brain.routeLane[route];
                        p.play = Play::Rush;
                        p.committed = true;
                        p.lurker = -1;
                        brain.site = p.site;
                    }
                    break;
                }
                case Order::GoA:
                case Order::GoB:
                    // (In now, by two of its ways: an execute.)
                    p.site = siteOk(order == Order::GoA ? brain.siteA : brain.siteB);
                    p.play = Play::Split;
                    p.committed = true;
                    p.lurker = -1;
                    brain.site = p.site;
                    break;
                case Order::Rush:
                    p.play = Play::Rush;
                    p.committed = true;
                    p.lurker = -1;
                    brain.site = p.site;
                    break;
                case Order::Split:
                    p.play = Play::Split;
                    p.committed = false;
                    p.holdMs = 0;
                    break;
                case Order::Default:
                    p.play = Play::Default;
                    p.committed = false;
                    p.commitAtMs = r.liveMs + 15000;
                    break;
                default: break;
                }
            }
            // (Each on its way again: from where it is.)
            for (int n = 0; n < kBotSlotsPerSide; ++n)
            {
                BotMind &m = brain.mind[side][n];
                m.viaDone = false;
                m.viaFresh = true;
                m.viaMs = 0;
                m.tries = 0;
                m.atStage = false;
                m.hasGoal = false;
            }
        }
    }

    /// A noise taken in: after the bot's reaction time, turned to; near
    /// enough (and it free to), gone to look.
    inline void botHears(AiCtx &c, const ActorWorld &a, const ActorState &bot, BotMind &mind, int index,
                         std::uint8_t team, const Round &r, int ms)
    {
        (void)a;
        if (mind.reactMs >= 0)
        {
            if ((mind.reactMs -= ms) > 0)
                return;
            mind.reactMs = -1;
            if (mind.noise < 0 || mind.noise >= kNoises || !ears.noise[mind.noise].active)
                return;
            const Noise &noise = ears.noise[mind.noise];
            aiMakeIdealYaw(c, noise.pos);
            aiChangeSchedule(c, &slAlertFace);
            mind.faceMs = 1300;
            mind.deafMs = 2500;
            const fx far = flatApart(noise.pos, bot.pos);
            const bool planted = r.phase == Phase::Planted;
            const int near = team == kTeamCT ? 1200 : 700;
            const bool free = index != r.botBomb && !(planted && team == kTeamCT) && !(planted && far > fxFromInt(500));
            // (A teammate down near: gone to, to trade -- as a shot heard.)
            if (free && far < fxFromInt(near) && noise.loud != 3)
            {
                for (int k = 0; k < 3; ++k)
                    mind.heard[k] = noise.pos[k];
                mind.heardMs = 6000;
            }
            return;
        }
        if (mind.deafMs > 0)
            return;
        int best = -1;
        fx bestFar = 0;
        for (int i = 0; i < kNoises; ++i)
        {
            const Noise &noise = ears.noise[i];
            if (!noise.active || noise.ageMs > kNoiseMs || noise.who == index)
                continue;
            const bool enemy = noise.team != team && noise.loud < 3;
            const bool told = noise.team == team && noise.loud >= 3;
            if (!enemy && !told)
                continue;
            const fx d[3] = {noise.pos[0] - bot.pos[0], noise.pos[1] - bot.pos[1], noise.pos[2] - bot.pos[2]};
            const fx far = fxLength(d);
            if (far < fxFromInt(hearReach(noise.loud)) && (best < 0 || far < bestFar))
            {
                best = i;
                bestFar = far;
            }
        }
        if (best < 0)
            return;
        mind.noise = best;
        const int skill = brain.skill >= 0 && brain.skill < kBotSkills ? brain.skill : 1;
        mind.reactMs = kBotReactMs[skill];
    }

    /// Where a bot's team's play wants it (`goal`), the site that is (or
    /// -1), and what to watch once there (`look`). False: nothing (a map
    /// with no sites).
    inline bool errandOf(const LevelView &view, const ActorWorld &a, const ActorState &bot, const BotRoster &bots,
                         const std::uint8_t *sideTeam, int side, int n, int index, std::uint8_t team, Round &r,
                         BotMind &mind, fx *goal, int &toSite, fx *look, bool &hasLook, int ms)
    {
        (void)a;
        (void)bots;
        (void)sideTeam;
        (void)side;
        if (brain.map.sites <= 0)
            return false;
        const bool live = r.phase == Phase::Live, planted = r.phase == Phase::Planted;
        auto put = [](fx *to, const fx *from)
        {
            for (int k = 0; k < 3; ++k)
                to[k] = from[k];
        };
        if (planted)
        {
            const fx feet[3] = {r.bomb[0], r.bomb[1], r.bomb[2] - fxFromInt(36)};
            put(goal, feet);
            if (team == kTeamCT && ctPlan.retakeMade && !ctPlan.retakeGo &&
                flatApart(bot.pos, feet) > fxFromInt(500))
            {
                put(goal, ctPlan.retake);
                put(look, feet);
                hasLook = true;
                return true;
            }
            if (team == kTeamT)
            {
                // (Guarding it: spread over its site's spots, watching
                // where the counter-terrorists come from.)
                int s = siteNear(feet, 2000);
                if (s >= 0 && brain.map.spots[s] > 0)
                    put(goal, brain.map.spot[s][(n * 3 + 1) % brain.map.spots[s]]);
                put(look, brain.homes ? brain.ctHome : feet);
                hasLook = true;
            }
            return true;
        }
        if (team == kTeamT && index == brain.fetcher && r.bombDropped)
        {
            put(goal, r.bombDrop);
            return true;
        }
        // A player's word to their side (HlCs.hpp's Order) -- not to the
        // one with the bomb: after them while they are up, along their way
        // (the first a point behind them, the next two, ...); or where each
        // was when told to hold, watching the way to the site (the
        // terrorists), or the way the terrorists come (the others).
        {
            const int ts = teamSlot(team);
            const Order told = teamOrder[ts];
            if (index != r.botBomb && ((told == Order::FollowMe && brain.leaderUp[ts]) || told == Order::Hold))
            {
                if (told == Order::Hold)
                {
                    put(goal, brain.held[ts][n % kBotSlotsPerSide]);
                    const int s = tPlan.site < brain.map.sites ? tPlan.site : 0;
                    if (team == kTeamT && brain.map.spots[s] > 0)
                    {
                        put(look, brain.map.spot[s][0]);
                        hasLook = true;
                    }
                    else if (team == kTeamCT && brain.homes)
                    {
                        put(look, brain.tHome);
                        hasLook = true;
                    }
                }
                else
                {
                    const int back = n % kBotSlotsPerSide + 1;
                    const int count = brain.trailCount[ts];
                    const int at = count - 1 - back >= 0 ? kBotSlotsPerSide - back : kBotSlotsPerSide + 1 - count;
                    put(goal, brain.trail[ts][at < 0 ? 0 : (at > kBotSlotsPerSide ? kBotSlotsPerSide : at)]);
                }
                return true;
            }
        }
        if (team == kTeamT)
        {
            const TeamPlan &p = tPlan;
            int site = p.committed ? p.site : (p.play == Play::Fake ? p.fake : p.site);
            site = site < brain.map.sites ? site : 0;
            // Its way: one of the site's (or, a default, of any site's,
            // spread out), by the play -- -1, the shortest, where that is
            // one of the choices.
            int wayFor = site;
            int way = -1;
            {
                const int routes = brain.autoLanes[site];
                const int choices = routes + (brain.shortestToo[site] ? 1 : 0);
                auto pickWay = [&](int c) { return brain.shortestToo[site] ? c - 1 : c; };
                if (choices > 0)
                {
                    switch (p.play)
                    {
                    case Play::Rush:
                        // (All one way; the shortest less often: it is where
                        // they are looked for.)
                        way = routes > 0 && (!brain.shortestToo[site] || (p.roll >> 12) % 10 < 7)
                                  ? static_cast<int>(p.roll % static_cast<std::uint32_t>(routes))
                                  : -1;
                        break;
                    case Play::Split:
                    {
                        // (Two ways, the team halved between them.)
                        const int first = static_cast<int>(p.roll % static_cast<std::uint32_t>(choices));
                        const int second =
                            choices > 1 ? (first + 1 + static_cast<int>((p.roll >> 4) % static_cast<std::uint32_t>(choices - 1))) % choices
                                        : first;
                        way = pickWay(((n + r.number) & 1) == 0 ? first : second);
                        break;
                    }
                    case Play::Default:
                    {
                        // (Spread over every way of every site, till they go
                        // in -- then from wherever they are, the shortest.)
                        if (!p.committed)
                        {
                            int all = 0;
                            for (int s2 = 0; s2 < brain.map.sites; ++s2)
                                all += brain.autoLanes[s2];
                            if (all > 0)
                            {
                                int k = static_cast<int>((static_cast<std::uint32_t>(n) * 7 + p.roll) % static_cast<std::uint32_t>(all));
                                for (int s2 = 0; s2 < brain.map.sites; ++s2)
                                {
                                    if (k < brain.autoLanes[s2])
                                    {
                                        wayFor = s2;
                                        way = k;
                                        break;
                                    }
                                    k -= brain.autoLanes[s2];
                                }
                            }
                        }
                        break;
                    }
                    case Play::Fake:
                        way = pickWay(static_cast<int>((static_cast<std::uint32_t>(n) + p.roll) % static_cast<std::uint32_t>(choices)));
                        break;
                    }
                }
            }
            // (A way told them, by the radio: all by it.)
            if (p.lane >= 0 && p.lane < brain.lanes[site])
            {
                wayFor = site;
                way = p.lane;
            }
            // The lurker: quietly at the end of one of the other site's
            // ways, till the bomb is down or late in the round; then in
            // with the rest.
            if (n == p.lurker && index != r.botBomb && brain.map.sites > 1 && r.liveMs < 60000)
            {
                const int other = 1 - site;
                if (brain.autoLanes[other] > 0)
                {
                    wayFor = other;
                    way = static_cast<int>((p.roll >> 8) % static_cast<std::uint32_t>(brain.autoLanes[other]));
                }
            }
            const bool lurking = n == p.lurker && wayFor != site;
            // Along its way, a point at a time.
            if (live && way >= 0 && !mind.viaDone)
            {
                const int steps = brain.laneSteps[wayFor][way];
                if (mind.viaFresh && steps > 0)
                {
                    // (Told another way mid-round: taken up at its nearest
                    // point -- or the next, if it is between the two.)
                    mind.viaFresh = false;
                    int best = 0;
                    fx bestFar = 0;
                    for (int k = 0; k < steps; ++k)
                    {
                        const fx far = floorApart(brain.lane[wayFor][way][k], bot.pos);
                        if (k == 0 || far < bestFar)
                        {
                            best = k;
                            bestFar = far;
                        }
                    }
                    if (best + 1 < steps && floorApart(brain.lane[wayFor][way][best + 1], bot.pos) <
                                                floorApart(brain.lane[wayFor][way][best + 1], brain.lane[wayFor][way][best]))
                        ++best;
                    // (Far from all of it -- told at the start -- from its
                    // first point.)
                    if (bestFar > fxFromInt(700))
                        best = 0;
                    mind.viaStep = best;
                }
                const fx *at = brain.lane[wayFor][way][mind.viaStep < steps ? mind.viaStep : steps - 1];
                mind.viaMs += ms;
                const bool last = mind.viaStep >= steps - 1;
                const fx atZ = at[2] - bot.pos[2];
                const bool reached = flatApart(at, bot.pos) < fxFromInt(140) && atZ < fxFromInt(128) && atZ > -fxFromInt(128);
                // (A duct's way in is not given up on as soon: there is no
                // other way through it.)
                bool ductNext = false;
                for (int d = 0; d < brain.ducts; ++d)
                    ductNext = ductNext || floorApart(at, brain.duct[d][0]) < fxFromInt(200);
                if (reached || mind.viaMs > (ductNext ? 45000 : 20000) || mind.tries >= (ductNext ? 6 : 3))
                {
                    mind.viaMs = 0;
                    mind.tries = 0;
                    // (At a duct's way in: through it, crawled -- botsThink.
                    // At the way's end, once the team goes in: inferno's
                    // balcony, down its stair to A.)
                    for (int d = 0; d < brain.ducts && reached && (!last || (p.committed && !lurking)); ++d)
                        if (floorApart(at, brain.duct[d][0]) < fxFromInt(200))
                        {
                            mind.crawlDuct = static_cast<std::int8_t>(d);
                            mind.crawlAt = 0;
                        }
                    if (!last)
                        ++mind.viaStep;
                    else if (p.committed && !lurking)
                        mind.viaDone = true;
                }
                // (At its way's end before the team goes in: held there.)
                const bool holding = last && (!p.committed || lurking) && flatApart(at, bot.pos) < fxFromInt(220);
                mind.atStage = holding && !lurking;
                if (!mind.viaDone)
                {
                    const fx *to = brain.lane[wayFor][way][mind.viaStep < steps ? mind.viaStep : steps - 1];
                    put(goal, to);
                    if (brain.map.spots[wayFor] > 0)
                    {
                        put(look, brain.map.spot[wayFor][0]);
                        hasLook = true;
                    }
                    return true;
                }
            }
            if (!p.committed && live)
            {
                // No way of its own (the shortest): gathered short of the
                // site, on the shortest way, till they go in.
                if (brain.stageOk[site])
                {
                    put(goal, brain.stage[site]);
                    mind.atStage = flatApart(bot.pos, brain.stage[site]) < fxFromInt(220);
                    if (brain.map.spots[site] > 0)
                    {
                        put(look, brain.map.spot[site][0]);
                        hasLook = true;
                    }
                    return true;
                }
            }
            toSite = site;
            const int spots = brain.map.spots[site];
            int pick = spots > 0 ? (n * 5 + side) % spots : 0;
            // (The one with the bomb, to the spot nearest the site's
            // middle: well in it.)
            if (index == r.botBomb)
            {
                const LogicRecord &box = view.logic[brain.map.record[site]];
                const fx mid[2] = {fxFromInt((box.mins[0] + box.maxs[0]) / 2), fxFromInt((box.mins[1] + box.maxs[1]) / 2)};
                fx best = 0;
                for (int k = 0; k < spots; ++k)
                {
                    const fx off[3] = {brain.map.spot[site][k][0] - mid[0], brain.map.spot[site][k][1] - mid[1], 0};
                    const fx far = fxLength(off);
                    if (k == 0 || far < best)
                    {
                        best = far;
                        pick = k;
                    }
                }
            }
            put(goal, brain.map.spot[site][pick]);
            if (brain.homes)
            {
                put(look, brain.ctHome);
                hasLook = true;
            }
            return true;
        }
        // A counter-terrorist: its place by the round's setup (a known
        // map's), or its site's spot; to the site called, if it is not that
        // site's anchor; an early place given up after 30 s, or once a site
        // is called.
        const CtPlan &p = ctPlan;
        // (A counter-terrorist player's word: a place of the map's to hold,
        // two of them by turns; off the sites, at their start, playing for
        // the retake; or, below, the site the bomb is going to, and
        // forward.)
        if (p.wayHold >= 0 && p.wayHold < ctWays)
        {
            const int h = brain.ctWayHold[p.wayHold][n % 2];
            if (h >= 0 && h < brain.holds)
            {
                put(goal, brain.hold[h]);
                put(look, brain.holdLook[h]);
                hasLook = true;
                toSite = brain.holdSite[h];
                return true;
            }
        }
        if (p.forRetake && brain.homes)
        {
            put(goal, brain.ctHome);
            put(look, brain.tHome);
            hasLook = true;
            return true;
        }
        int site = n < kBotSlotsPerSide ? p.site[n] : 0;
        const bool told = p.told >= 0;
        const int calledSite = told ? p.told : p.called;
        const bool calledNow = told || (p.called >= 0 && r.liveMs - p.calledMs < 20000);
        // (Told where the bomb is going: all of them to it, anchors too.)
        const bool anchor = n < kBotSlotsPerSide && p.anchor[n] && !told;
        int hold = brain.holds > 0 && n < kBotSlotsPerSide ? brain.setup[(p.push ? 3 : p.setup) % kCtSetups][n] : -1;
        // (Told where the bomb is going: one of that site's places, by
        // turns, if its own is the other's.)
        if (told && hold >= 0 && brain.holdSite[hold] != p.told)
        {
            int ofSite[kHoldsMost];
            int count = 0;
            for (int h = 0; h < brain.holds; ++h)
                if (brain.holdSite[h] == p.told && !brain.holdEarly[h])
                    ofSite[count++] = h;
            hold = count > 0 ? ofSite[n % count] : -1;
        }
        if (hold >= 0)
            site = brain.holdSite[hold];
        if (calledNow && !anchor)
            site = calledSite;
        site = site < brain.map.sites ? site : 0;
        if (hold >= 0 && !(calledNow && !anchor && calledSite != brain.holdSite[hold]) &&
            !(brain.holdEarly[hold] && !p.push && (r.liveMs > 30000 || calledNow)))
        {
            put(goal, brain.hold[hold]);
            put(look, brain.holdLook[hold]);
            hasLook = true;
            return true;
        }
        if (hold < 0 && n < kBotSlotsPerSide && (p.forward[n] || p.push) && (r.liveMs < 25000 || p.push) && !calledNow &&
            brain.stageOk[site])
        {
            put(goal, brain.stage[site]);
            put(look, brain.homes ? brain.tHome : brain.stage[site]);
            hasLook = brain.homes;
            return true;
        }
        toSite = site;
        const int spots = brain.map.spots[site];
        put(goal, brain.map.spot[site][spots > 0 ? (n * 5 + side) % spots : 0]);
        if (brain.stageOk[site])
        {
            put(look, brain.stage[site]);
            hasLook = true;
        }
        else if (brain.homes)
        {
            put(look, brain.tHome);
            hasLook = true;
        }
        return true;
    }

    /// The bots' part of a frame of the round (after the monsters' own
    /// think): errands given, the bomb carried, planted, defused.
    inline BotOut botsThink(LogicWorld &w, PlayerState &player, const LevelView &view, const BotRoster &bots,
                            std::uint8_t playerTeam, Round &r, PlayerCs &cs, int ms)
    {
        BotOut out;
        if (w.actors == nullptr || w.world == nullptr || w.actors->states == nullptr || w.actors->ai == nullptr)
            return out;
        ActorWorld &a = *w.actors;
        takeWorld = &a;
        for (int k = 0; k < 3; ++k)
            takePlayer[k] = player.origin[k];
        const std::uint8_t sideTeam[2] = {static_cast<std::uint8_t>(playerTeam == kTeamT ? kTeamCT : kTeamT),
                                          playerTeam};
        const bool live = r.phase == Phase::Live, planted = r.phase == Phase::Planted;
        // (The other ways to the sites: once, from where a terrorist starts.)
        if (live && !brain.viaMade && brain.map.sites > 0)
        {
            const int tSide = sideTeam[0] == kTeamT ? 0 : 1;
            if (brain.homes)
                makeVias(a, *w.world, brain.tHome);
            else if (bots.count[tSide] > 0)
                makeVias(a, *w.world, a.states[bots.slot[tSide][0]].pos);
            else if (playerTeam == kTeamT)
                makeVias(a, *w.world, player.origin);
        }

        // The round over: each bot paid as the player is, and one shot down
        // has lost its gun.
        if (r.phase == Phase::Over && !brain.paid)
        {
            brain.paid = true;
            const bool objective = r.why == Why::TargetBombed || r.why == Why::BombDefused;
            for (int side = 0; side < 2; ++side)
            {
                const std::uint8_t team = sideTeam[side];
                const int losses = team == kTeamT ? r.lossesT : r.lossesCT;
                const int pay = team == r.winner ? (objective ? kWinObjective : kWinElimination)
                                                 : lossBonus(losses) + (team == kTeamT && r.plantedThisRound
                                                                            ? kPlantedLossBonus
                                                                            : 0);
                for (int n = 0; n < kBotSlotsPerSide; ++n)
                {
                    BotPurse &p = brain.purse[side][n];
                    p.money = p.money + pay > kMoneyMost ? kMoneyMost : p.money + pay;
                    if (n < bots.count[side] && !botUp(a.states[bots.slot[side][n]]))
                    {
                        p.gun = botPistol(team);
                        p.armor = 0;
                        p.grenade = kGearNone;
                    }
                }
            }
        }

        // The bomb, while nobody plants it: its carrier shot down, it lies
        // where they fell, till a terrorist walks over it -- a bot sent for
        // it (the nearest), or the player.
        brain.fetcher = -1;
        if (live && r.bombMap)
        {
            const bool carrierDown = r.botBomb >= 0 && !botUp(a.states[r.botBomb]);
            const bool playerLost = r.hasBomb && player.health <= 0;
            if (carrierDown || playerLost)
            {
                const fx *at = carrierDown ? a.states[r.botBomb].pos : player.origin;
                for (int k = 0; k < 3; ++k)
                    r.bombDrop[k] = at[k];
                if (playerLost)
                    r.bombDrop[2] -= fxFromInt(player.ducked ? 18 : 36);
                r.bombDropped = true;
                r.botBomb = -1;
                r.hasBomb = false;
                c4InHand = false;
            }
            if (r.bombDropped)
            {
                fx nearest = 0;
                for (int side = 0; side < 2; ++side)
                {
                    if (sideTeam[side] != kTeamT)
                        continue;
                    for (int n = 0; n < bots.count[side]; ++n)
                    {
                        const int index = bots.slot[side][n];
                        if (!botUp(a.states[index]))
                            continue;
                        const fx far = flatApart(a.states[index].pos, r.bombDrop);
                        const fx up = a.states[index].pos[2] - r.bombDrop[2];
                        if (far < fxFromInt(48) && up < fxFromInt(64) && up > -fxFromInt(64))
                        {
                            r.botBomb = index; // (picked up)
                            r.bombDropped = false;
                            break;
                        }
                        if (brain.fetcher < 0 || far < nearest)
                        {
                            brain.fetcher = index;
                            nearest = far;
                        }
                    }
                }
                // (The player, a terrorist, walking over it.)
                const fx feet[3] = {player.origin[0], player.origin[1], player.origin[2] - fxFromInt(36)};
                const fx up = feet[2] - r.bombDrop[2];
                if (r.bombDropped && playerTeam == kTeamT && player.health > 0 &&
                    flatApart(feet, r.bombDrop) < fxFromInt(48) && up < fxFromInt(64) && up > -fxFromInt(64))
                {
                    r.hasBomb = true;
                    r.bombDropped = false;
                }
                if (!r.bombDropped)
                    brain.fetcher = -1;
            }
        }

        for (std::uint16_t &left : spottedMs)
            left = left > ms ? static_cast<std::uint16_t>(left - ms) : 0;
        botsListen(a, player, bots, sideTeam, playerTeam, r, ms);
        if (planted)
            retakeTick(a, *w.world, bots, sideTeam, r);
        ordersTick(a, bots, sideTeam, playerTeam, player, r);
        if (live)
            playsTick(a, bots, sideTeam, r, ms);
        // (The player's own death: whose it is.)
        {
            const bool up = player.health > 0;
            if (!up && scores.playerUp)
            {
                ++scores.player.deaths;
                creditKill(bots, sideTeam, csPlayerHitBy, playerTeam);
                const fx feet[3] = {player.origin[0], player.origin[1], player.origin[2] - fxFromInt(36)};
                dropGun(feet, cs.primary);
            }
            scores.playerUp = up;
            if (!up || player.health >= 100)
                csPlayerHitBy = -1;
        }
        for (int side = 0; side < 2; ++side)
        {
            const std::uint8_t team = sideTeam[side];
            for (int n = 0; n < bots.count[side]; ++n)
            {
                const int index = bots.slot[side][n];
                ActorState &bot = a.states[index];
                BotMind &mind = brain.mind[side][n];
                if (mind.wasUp && !botUp(bot) && bot.active)
                    scoreDeath(a, bots, sideTeam, side, n, index);
                botSounds(w, view, bot, mind, ms, index, team);
                AiMonster *m = aiOf(a, index);
                const AiKind *kind = aiKindOf(bot.kind);
                if (!botUp(bot) || m == nullptr || kind == nullptr || !m->started || bot.script >= 0)
                {
                    mind.workMs = 0;
                    continue;
                }
                if (!mind.fitted && m->maxHealth != 0)
                {
                    mind.fitted = true;
                    m->maxHealth = kBotHealth;
                    bot.health = kBotHealth;
                }
                // In a duct (a vent): crawled through, point to point, at a
                // crawl's pace -- nothing else till out of it.
                if (mind.crawlDuct >= 0 && mind.crawlDuct < brain.ducts)
                {
                    // (Standing, as its own legs have it: the crawl moves it.)
                    {
                        AiCtx still = aiCtx(w, player, index, *m, *kind);
                        const AiSchedule *stand = aiScheduleOfType(still, kSchedIdleStand);
                        if (m->schedule != stand)
                            aiChangeSchedule(still, stand);
                    }
                    const int d = mind.crawlDuct;
                    const fx *to = brain.duct[d][mind.crawlAt];
                    const fx gap[3] = {to[0] - bot.pos[0], to[1] - bot.pos[1], to[2] - bot.pos[2]};
                    const fx far = fxLength(gap);
                    const fx step = fxFromInt(brain.ductPace[d] > 0 ? brain.ductPace[d] : 110) * ms / 1000;
                    if (far <= step || far <= 0)
                    {
                        for (int k = 0; k < 3; ++k)
                            bot.pos[k] = to[k];
                        if (++mind.crawlAt >= brain.ductLen[d])
                        {
                            mind.crawlDuct = -1;
                            mind.hasGoal = false;
                            mind.againMs = 0;
                        }
                    }
                    else
                        for (int k = 0; k < 3; ++k)
                            bot.pos[k] += static_cast<fx>(static_cast<std::int64_t>(gap[k]) * step / far);
                    for (int k = 0; k < 3; ++k)
                    {
                        bot.target[k] = bot.pos[k];
                        mind.was[k] = bot.pos[k];
                    }
                    bot.speed = 0;
                    mind.stillMs = 0;
                    mind.stuckMs = 0;
                    m->enemy = kAiNone;
                    continue;
                }
                AiCtx c = aiCtx(w, player, index, *m, *kind);
                // (An enemy shot down is no enemy: on with the round.)
                if (m->enemy >= 0 && m->enemy < a.count && !botUp(a.states[m->enemy]))
                {
                    m->enemy = kAiNone;
                    if (m->state == kStateCombat)
                        m->idealState = kStateAlert;
                }
                else if (m->enemy == kAiPlayer && player.health <= 0)
                {
                    m->enemy = kAiNone;
                    if (m->state == kStateCombat)
                        m->idealState = kStateAlert;
                }
                // (An enemy out of sight a while, its last place far off:
                // let go, for the round's errand -- not hunted.)
                if (m->enemy != kAiNone && aiHas(c, kCondSeeEnemy))
                    mind.unseenMs = 0;
                else if (m->enemy != kAiNone && (mind.unseenMs += ms) >= kLostMs &&
                         aiDist2D(m->enemyLkp, bot.pos) > fxFromInt(kLostReach))
                {
                    m->enemy = kAiNone;
                    mind.unseenMs = 0;
                    if (m->state == kStateCombat)
                        m->idealState = kStateAlert;
                }
                // (On the radar: an enemy one of the player's side sees, and
                // one of theirs firing -- heard.)
                if (m->state == kStateCombat && m->enemy != kAiNone && aiHas(c, kCondSeeEnemy))
                {
                    if (side == 1 && m->enemy >= 0 && m->enemy < kBotGunActors)
                        spottedMs[m->enemy] = 1500;
                    if (side == 0 && index < kBotGunActors)
                        spottedMs[index] = 1000;
                }
                bool fighting = m->state == kStateCombat && m->enemy != kAiNone;
                const int healthWas = mind.health;
                if (mind.faceMs > 0)
                    mind.faceMs -= ms;
                if (mind.deafMs > 0)
                    mind.deafMs -= ms;
                // (Seen: told to the others near; and, near a site, the
                // site called.)
                if (fighting && aiHas(c, kCondSeeEnemy))
                {
                    const fx *at = m->enemy == kAiPlayer ? player.origin
                                   : (m->enemy >= 0 && m->enemy < a.count ? a.states[m->enemy].pos : nullptr);
                    if (at != nullptr)
                    {
                        hearNoise(at, index, team, 3);
                        seenAt(at, team, r);
                    }
                }
                // Its grenade, thrown at who it fights, as the game's bots
                // throw theirs: now and then, at a fair distance, in an arc
                // that gets there.
                BotPurse &purse = brain.purse[side][n];
                if (fighting && purse.grenade != kGearNone && (mind.grenadeMs -= ms) <= 0)
                {
                    mind.grenadeMs = 1500;
                    const fx *at = m->enemy == kAiPlayer ? player.origin
                                   : (m->enemy >= 0 && m->enemy < a.count ? a.states[m->enemy].pos : nullptr);
                    if (at != nullptr && botRandom() % 100 < 30)
                    {
                        const fx flat[3] = {at[0] - bot.pos[0], at[1] - bot.pos[1], 0};
                        const fx far = fxLength(flat);
                        if (far > fxFromInt(300) && far < fxFromInt(1000))
                        {
                            fx from[3] = {bot.pos[0], bot.pos[1], bot.pos[2] + fxFromInt(28)};
                            fx to[3] = {at[0], at[1], at[2]};
                            fx velocity[3];
                            if (grunt::vecCheckToss(c, from, to, 50, velocity) &&
                                botThrow(a, index, purse.grenade, from, velocity))
                            {
                                purse.grenade = kGearNone;
                                if (w.heard != nullptr && brain.view != nullptr)
                                {
                                    const std::uint16_t say = findSound(*brain.view, CsSound::FireInTheHole);
                                    if (say != kNoSound)
                                        w.heard->add(say, bot.pos, 127, kRadiusMedium);
                                }
                            }
                        }
                    }
                }
                // Shot, by one unseen: round to them, and at them.
                if (!fighting && healthWas > 0 && bot.health < healthWas && index < 256)
                {
                    const int by = scores.hitBy[index];
                    const fx *at = by == kAiPlayer ? player.origin : (by >= 0 && by < a.count ? a.states[by].pos : nullptr);
                    if (at != nullptr)
                    {
                        aiMakeIdealYaw(c, at);
                        m->enemy = static_cast<std::int16_t>(by);
                        for (int k = 0; k < 3; ++k)
                            m->enemyLkp[k] = at[k];
                        m->idealState = kStateCombat;
                        aiChangeSchedule(c, aiScheduleOfType(c, kSchedCombatFace));
                        mind.faceMs = 1000;
                        mind.reactMs = -1;
                    }
                }
                // A noise: taken in after its reaction time, then turned
                // to -- and, near enough, gone to.
                else if (!fighting && (live || planted))
                    botHears(c, a, bot, mind, index, team, r, ms);
                // (A fight that comes to nothing -- each in sight of the
                // other, neither able to hit -- is given up a while.)
                {
                    int enemyHealth = 0;
                    if (m->enemy >= 0 && m->enemy < a.count)
                        enemyHealth = a.states[m->enemy].health;
                    else if (m->enemy == kAiPlayer)
                        enemyHealth = player.health;
                    const bool hurt = bot.health != mind.health || enemyHealth != mind.enemyHealth;
                    mind.health = bot.health;
                    mind.enemyHealth = enemyHealth;
                    mind.fightMs = fighting && !hurt ? mind.fightMs + ms : 0;
                    if (mind.fightMs >= kFightIdleMs)
                    {
                        mind.fightMs = 0;
                        mind.pushMs = kPushMs;
                        mind.pushFrom = m->enemy;
                        mind.againMs = 0;
                        mind.hasGoal = false;
                    }
                    if (mind.pushMs > 0)
                    {
                        mind.pushMs -= ms;
                        // (... but not past another enemy seen, nor this one
                        // come near: those are fought, faced at once.)
                        const fx *them = m->enemy == kAiPlayer ? player.origin
                                         : m->enemy >= 0 && m->enemy < a.count ? a.states[m->enemy].pos
                                                                              : nullptr;
                        const bool fresh = fighting && aiHas(c, kCondSeeEnemy) && them != nullptr &&
                                           (m->enemy != mind.pushFrom || aiDist2D(them, bot.pos) < fxFromInt(kPushNear));
                        fighting = fighting && (hurt || fresh);
                        if (hurt || fresh)
                            mind.pushMs = 0;
                        if (fresh && m->schedule == &slCsPush)
                        {
                            aiMakeIdealYaw(c, them);
                            aiChangeSchedule(c, aiScheduleOfType(c, kSchedCombatFace));
                        }
                    }
                }

                // At the bomb's work: planting, in a site; defusing, by it.
                // (Not while fighting; and standing still.)
                bool working = false;
                if (!fighting)
                {
                    // (A site is judged by a player's middle, 36 over the feet.)
                    const fx middle[3] = {bot.pos[0], bot.pos[1], bot.pos[2] + fxFromInt(36)};
                    const bool planting = live && index == r.botBomb && inBombSite(view, middle);
                    const bool defusing = planted && team == kTeamCT && nearBomb(r, middle);
                    // (Stopped where it is, for the work.)
                    if ((planting || defusing) &&
                        (aiIsMoving(c) || m->schedule == &slCsErrand || m->schedule == &slCsPush))
                    {
                        aiRouteClear(c);
                        aiChangeSchedule(c, aiScheduleOfType(c, kSchedIdleStand));
                    }
                    if (planting)
                    {
                        working = true;
                        hearNoise(bot.pos, index, team, 2);
                        mind.workMs += ms;
                        if (mind.workMs >= kPlantMs)
                        {
                            r.phase = Phase::Planted;
                            r.phaseMs = 0;
                            r.botBomb = -1;
                            r.plantedThisRound = true;
                            r.bombMs = kC4Ms;
                            r.beepMs = 0;
                            r.plantMs = 0;
                            for (int k = 0; k < 3; ++k)
                                r.bomb[k] = bot.pos[k];
                            r.bomb[2] += fxFromInt(36);
                            out.planted = true;
                            mind.workMs = 0;
                            working = false;
                        }
                    }
                    else if (defusing)
                    {
                        {
                            working = true;
                            out.defusing = true;
                            mind.workMs += ms;
                            if (mind.workMs >= kDefuseMs)
                            {
                                out.defused = true;
                                endRound(r, cs, kTeamCT, Why::BombDefused);
                                return out;
                            }
                        }
                    }
                }
                if (!working)
                    mind.workMs = 0;
                if (working || fighting || (!live && !planted) || mind.faceMs > 0)
                    continue;

                // Its errand: where the round wants it (its team's play).
                fx goal[3];
                fx look[3];
                bool hasLook = false;
                int toSite = -1;
                if (mind.heardMs > 0)
                {
                    // (Gone to look where it heard something.)
                    mind.heardMs -= ms;
                    for (int k = 0; k < 3; ++k)
                        goal[k] = mind.heard[k];
                    const fx left[3] = {goal[0] - bot.pos[0], goal[1] - bot.pos[1], 0};
                    if (fxLength(left) < fxFromInt(96))
                        mind.heardMs = 0;
                }
                else if (!errandOf(view, a, bot, bots, sideTeam, side, n, index, team, r, mind, goal, toSite, look, hasLook, ms))
                    continue; // (a map with no sites: the game's own idling)
                // Held up: a while off to one side, then on again. (Not
                // where it was going: there, holding a place, standing is
                // what it is to do.)
                const bool atGoal = flatApart(goal, bot.pos) < fxFromInt(96) ||
                                    (toSite >= 0 && index != r.botBomb &&
                                     inSiteBox(view.logic[brain.map.record[toSite]], bot.pos, 0));
                if (atGoal)
                {
                    for (int k = 0; k < 3; ++k)
                        mind.was[k] = bot.pos[k];
                    mind.stillMs = 0;
                    mind.stuckMs = 0;
                    mind.detourMs = 0;
                }
                else
                {
                    const fx moved[3] = {bot.pos[0] - mind.was[0], bot.pos[1] - mind.was[1], 0};
                    // (On an errand, or failing to set out on one: either
                    // way not getting anywhere.)
                    if (fxLength(moved) > fxFromInt(24))
                    {
                        for (int k = 0; k < 3; ++k)
                            mind.was[k] = bot.pos[k];
                        mind.stillMs = 0;
                        mind.stuckMs = 0;
                    }
                    else if ((mind.stuckMs += ms) >= 9000)
                    {
                        // (Caught fast, nothing it tries getting it free: put
                        // on the next node of its way -- or the nearest, if
                        // not on it -- as a player would jump out.)
                        mind.stuckMs = 0;
                        int at = pathNearest(a, *w.world, bot.pos);
                        if (at >= 0 && flatApart(a.nodes[at].pos, bot.pos) < fxFromInt(40))
                        {
                            const int to = pathNearest(a, *w.world, goal);
                            const int next = to >= 0 && to != at ? pathStep(a, at, to, kPathLinkHuman, false) : kNoPathNode;
                            at = next != kNoPathNode ? next : -1;
                        }
                        if (at >= 0 && flatApart(a.nodes[at].pos, bot.pos) < fxFromInt(320))
                        {
                            bool free = true;
                            for (int o = 0; o < a.count && free; ++o)
                                free = o == index || !a.states[o].active ||
                                       flatApart(a.states[o].pos, a.nodes[at].pos) > fxFromInt(32);
                            if (free)
                            {
                                for (int k = 0; k < 3; ++k)
                                    bot.pos[k] = a.nodes[at].pos[k];
                                aiRouteClear(c);
                                mind.againMs = 0;
                                mind.hasGoal = false;
                            }
                        }
                    }
                    else if ((mind.stillMs += ms) >= 2500)
                    {
                        mind.stillMs = 0;
                        // (Held up by one of its own, in a narrow way -- a
                        // stair: the later steps back a moment, out of the
                        // other's way, and lets it by; the way is not
                        // blamed for it.)
                        int crowd = -1;
                        for (int o2 = 0; o2 < bots.count[side] && o2 < n && crowd < 0; ++o2)
                        {
                            const ActorState &them = a.states[bots.slot[side][o2]];
                            const fx dz = them.pos[2] - bot.pos[2];
                            if (them.active && them.health > 0 && flatApart(them.pos, bot.pos) < fxFromInt(48) &&
                                dz < fxFromInt(64) && dz > -fxFromInt(64))
                                crowd = bots.slot[side][o2];
                        }
                        if (crowd >= 0)
                        {
                            const fx off[3] = {bot.pos[0] - a.states[crowd].pos[0], bot.pos[1] - a.states[crowd].pos[1], 0};
                            const fx far = fxLength(off);
                            fx back[3] = {bot.pos[0], bot.pos[1], bot.pos[2]};
                            if (far > 0)
                                for (int k = 0; k < 2; ++k)
                                    back[k] += static_cast<fx>(static_cast<std::int64_t>(off[k]) * fxFromInt(72) / far);
                            if (walkClear(*w.world, nullptr, -1, kWalkStanding, bot.pos, back, 8))
                                for (int k = 0; k < 3; ++k)
                                    mind.detour[k] = back[k];
                            else
                                for (int k = 0; k < 3; ++k)
                                    mind.detour[k] = bot.pos[k];
                            mind.detourMs = 1800;
                        }
                        else
                        {
                        // (Held up on the same link twice: it is not to
                        // be walked after all -- taken out of the ways.)
                        const int from = pathNearest(a, *w.world, bot.pos);
                        const int to = pathNearest(a, *w.world, goal);
                        const int next = from >= 0 && to >= 0 && from != to ? pathStep(a, from, to, kPathLinkHuman, false)
                                                                             : kNoPathNode;
                        const int link = next != kNoPathNode ? pathLinkBetween(a, from, next) : -1;
                        const fx nearFrom = from >= 0 ? flatApart(a.nodes[from].pos, bot.pos) : 0;
                        if (link >= 0 && link == mind.stuckLink && nearFrom < fxFromInt(160))
                        {
                            if (++mind.stuckTimes >= 3)
                            {
                                // (Only if there is still a way without it.)
                                const int count = walkBlockedCount;
                                walkBlockLink(link);
                                walkBlockLink(pathLinkBetween(a, next, from));
                                if (pathStep(a, from, to, kPathLinkHuman, false) == kNoPathNode)
                                    walkBlockedCount = count;
                                mind.stuckTimes = 0;
                                mind.againMs = 0;
                                mind.hasGoal = false;
                            }
                        }
                        else
                        {
                            mind.stuckLink = link;
                            mind.stuckTimes = 1;
                        }
                        // (Short of its node, held up by a narrow way --
                        // dust2's mid doors -- that is clear from the node
                        // itself: put on it, a step, and on again from there.)
                        bool centred = false;
                        if (mind.stuckTimes >= 1 && mind.stuckTimes <= 2 && from >= 0 && nearFrom > fxFromInt(2) &&
                            nearFrom < fxFromInt(28))
                        {
                            bool free = true;
                            for (int o = 0; o < a.count && free; ++o)
                                free = o == index || !a.states[o].active ||
                                       flatApart(a.states[o].pos, a.nodes[from].pos) > fxFromInt(32);
                            if (free)
                            {
                                for (int k = 0; k < 3; ++k)
                                    bot.pos[k] = a.nodes[from].pos[k];
                                aiRouteClear(c);
                                mind.againMs = 0;
                                mind.hasGoal = false;
                                centred = true;
                            }
                        }
                        if (!centred && botDetour(a, *w.world, index, mind.tries++ + n, mind.detour))
                            mind.detourMs = 3000;
                        }
                    }
                    if (mind.detourMs > 0)
                    {
                        mind.detourMs -= ms;
                        // (Near it is there -- as an errand's place is: not
                        // stood about short of it until the time is up.)
                        const fx left[3] = {mind.detour[0] - bot.pos[0], mind.detour[1] - bot.pos[1], 0};
                        if (fxLength(left) < fxFromInt(64))
                            mind.detourMs = 0;
                        else
                        {
                            for (int k = 0; k < 3; ++k)
                                goal[k] = mind.detour[k];
                            toSite = -1;
                        }
                    }
                }
                const fx d[3] = {goal[0] - bot.pos[0], goal[1] - bot.pos[1], 0};
                // (In the site it was sent to is there: it holds it -- but
                // the one with the bomb goes on in, to plant.)
                const bool inSite = toSite >= 0 && index != r.botBomb &&
                                    inSiteBox(view.logic[brain.map.record[toSite]], bot.pos, 0);
                // (... and the one with it is there once in the site.)
                const fx middle[3] = {bot.pos[0], bot.pos[1], bot.pos[2] + fxFromInt(36)};
                const bool carrierIn = live && index == r.botBomb && inBombSite(view, middle);
                const bool there = inSite || carrierIn || (index != r.botBomb && fxLength(d) < fxFromInt(64)) ||
                                   fxLength(d) < fxFromInt(16);
                const bool sameGoal = mind.hasGoal && goal[0] == mind.goal[0] && goal[1] == mind.goal[1];
                const AiSchedule *errand = mind.pushMs > 0 ? &slCsPush : &slCsErrand;
                if (there)
                {
                    // (Holding a place: turned now and then to where the
                    // enemy would come from, and a little either side.)
                    if (hasLook && (mind.lookMs -= ms) <= 0)
                    {
                        mind.lookMs = 2500 + static_cast<int>(botRandom() % 2500);
                        fx at[3] = {look[0], look[1], look[2]};
                        const int sway = static_cast<int>(botRandom() % 3) - 1;
                        at[0] += fxFromInt(sway * 256);
                        at[1] -= fxFromInt(sway * 256);
                        aiMakeIdealYaw(c, at);
                        aiChangeSchedule(c, &slAlertFace);
                        mind.faceMs = 1200;
                    }
                    continue;
                }
                if (m->schedule == errand && sameGoal)
                    continue;
                if ((mind.againMs -= ms) > 0 && sameGoal)
                    continue;
                mind.againMs = 1500;
                for (int k = 0; k < 3; ++k)
                    mind.goal[k] = m->moveGoal[k] = goal[k];
                mind.hasGoal = true;
                aiChangeSchedule(c, errand);
            }
        }
        return out;
    }
} // namespace hldata::cs
