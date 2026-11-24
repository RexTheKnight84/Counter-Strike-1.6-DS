#pragma once

// Counter-Strike's round (as its 1.6 has it, by default): a few seconds of
// freeze time to buy in, the round's clock, the bomb -- the terrorists carry
// it, plant it at a bomb site, and it goes off unless it is defused -- who
// wins and why, the money each team gets for it, and the next round.
//
// What a player who lived through a round carries comes with them into the
// next; one who died starts again with a pistol, and keeps their money.
//
// Bots are not here yet. A team with nobody on it is not "eliminated": with
// no bots the round ends by its clock, the bomb, or the player's death.
//
// std-only and inline: the host tests run it (tests/test_cs.cpp). The
// console's game asks it each frame (dll/hl/Game.arm.cpp) and draws what it
// says (render/BottomScreen).

#include <cstdint>
#include <cstdio>

#include "HlCs.hpp"
#include "HlSound.hpp"

namespace hldata::cs
{
    // The game's own defaults (mp_freezetime, mp_buytime, mp_c4timer...).
    inline constexpr int kFreezeMs = 6000;
    inline constexpr int kBuyMs = 90000;       ///< after the freeze
    inline constexpr int kRoundOverMs = 5000;  ///< from the round's end to the next
    inline constexpr int kC4Ms = 35000;
    inline constexpr int kPlantMs = 3000;
    inline constexpr int kDefuseMs = 10000;
    inline constexpr int kDefuseKitMs = 5000;
    inline constexpr int kDefuseReach = 72;    ///< units from the bomb
    inline constexpr int kBombRadiusDefault = 500;
    inline constexpr int kLossFirst = 1400, kLossStep = 500, kLossMost = 3400;
    inline constexpr int kWinElimination = 3250, kWinObjective = 3500, kPlantedLossBonus = 800;

    enum class Phase : std::uint8_t
    {
        Freeze,  ///< bought in, nobody moves
        Live,
        Planted, ///< the bomb is down: the clock is the bomb's
        Over,    ///< who won is said; the next round soon
    };

    enum class Why : std::uint8_t
    {
        None,
        TargetBombed,     ///< the bomb went off: the terrorists'
        BombDefused,      ///< the counter-terrorists'
        TerroristsDead,   ///< all of them: the counter-terrorists'
        CtsDead,          ///< the terrorists'
        TargetSaved,      ///< the clock ran out with no bomb down: the counter-terrorists'
        HostagesNotSaved, ///< ... on a map with no bomb site: the terrorists'
    };

    struct Round
    {
        Phase phase = Phase::Freeze;
        int number = 0;
        int phaseMs = 0;  ///< time spent in this phase
        int liveMs = 0;   ///< since the freeze ended
        int clockMs = 0;  ///< left on the round's clock
        int roundMs = 2 * 60000;
        int bombMs = 0;   ///< left on the bomb's
        int bombRadius = kBombRadiusDefault;
        bool bombMap = false; ///< the map has a bomb site
        fx bomb[3] = {0, 0, 0}; ///< planted: 36 over the floor it lies on (a standing player's middle)
        int plantMs = 0;  ///< how long the player has been planting
        int defuseMs = 0; ///< ... defusing
        int beepMs = 0;   ///< to the bomb's next beep
        std::uint8_t winner = 0; ///< kTeam*, as the round ended
        Why why = Why::None;
        int scoreT = 0, scoreCT = 0;
        int lossesT = 0, lossesCT = 0; ///< in a row
        bool plantedThisRound = false;
        bool hasBomb = false; ///< the player carries it
        int botBomb = -1;     ///< ... or a bot does: its actor (HlCsBots.hpp)
        bool bombDropped = false; ///< ... or it lies where its carrier fell
        fx bombDrop[3] = {0, 0, 0};
    };
    inline Round round;
    /// The bomb is in the player's hand (slot 5, as the game has it): fire,
    /// held in a bomb site, plants it. The weapon put away for it, to take
    /// back after.
    inline bool c4InHand = false;
    /// The bots are to be woken (and the last round's put away): set at
    /// every start of the player (a new game, a new round), done by the
    /// game's next frame (dll/hl/Game.arm.cpp).
    inline bool botsDue = false;
    inline std::uint8_t c4PutAway = 0;

    /// A new game: round 1, frozen. `bombRadius` the map's (0: the game's).
    inline void newGame(Round &r, const Setup &s, int bombRadius, bool bombMap)
    {
        r = Round();
        r.roundMs = (s.roundMinutes >= kRoundMinutesLeast ? s.roundMinutes : 2) * 60000;
        r.bombRadius = bombRadius > 0 ? bombRadius : kBombRadiusDefault;
        r.bombMap = bombMap;
        r.hasBomb = bombMap && s.team == kTeamT;
        c4InHand = false;
    }

    /// What the round needs to know of this frame.
    struct RoundIn
    {
        int ms = 0;
        bool alive = true;     ///< the player
        std::uint8_t team = kTeamCT;
        const fx *origin = nullptr;
        bool ducked = false;   ///< the player's crouched box: `origin` 18 over the feet, not 36
        bool inBombSite = false;
        bool actionHeld = false; ///< PLANT or DEFUSE held (the BUY button's place, or USE)
        bool hasKit = false;
        // The bots, when there are any: how many on each side of the
        // player's, and how many of them alive.
        int matesPlaying = 0, matesAlive = 0;
        int enemiesPlaying = 0, enemiesAlive = 0;
    };

    /// What happened this frame.
    struct RoundOut
    {
        bool started = false;  ///< a new round: everyone back to their start (not the first: newGame)
        bool unfrozen = false;
        bool planted = false;
        bool exploded = false; ///< `blast` is what it does to the player
        int blast = 0;
        bool defused = false;
        bool ended = false;    ///< the round won (winner, why)
        bool beep = false;
    };

    /// The round's own sounds, from the game's own files (the converter adds
    /// them to each of its maps' sounds: EntityCook): the bomb's beeps --
    /// higher and closer together as it runs down -- its plant, its defuse,
    /// its blast; and the radio's word of each.
    enum class CsSound : std::uint8_t
    {
        Beep1,
        Beep2,
        Beep3,
        Beep4,
        Beep5,
        Plant,
        Disarm,
        Disarmed,
        Explode,
        RadioPlanted,
        RadioDefused,
        RadioTerroristsWin,
        RadioCtsWin,
        Die1,
        Die2,
        Die3,
        FireInTheHole,
        Flash1,
        Flash2,
        SmokeBurst, ///< a smoke grenade going off
        NvgOn,
        NvgOff,
        NvgEquip,
        ShieldHit, ///< a round on the tactical shield
        FireM4a1Silenced, FireUspSilenced, ///< their shots, the silencer on
        M4a1SilencerOn, M4a1SilencerOff, UspSilencerOn, UspSilencerOff,
        // The player's orders to their team (HlCs.hpp: Order), as said.
        RadioGo, RadioStormFront, RadioFlank, RadioGetInPos, RadioFollowMe, RadioHold, RadioRoger,
        // ... a counter-terrorist's: the enemy spotted (BOMB A, B), regroup
        // (RETAKE), take the point (a place to hold).
        RadioEnemySpot, RadioRegroup, RadioTakePoint,
        // Each gun's shot, in HlCs.hpp's kGuns order (fireSoundOf).
        FireGlock, FireUsp, FireP228, FireDeagle, FireFiveSeven, FireElite, FireM3, FireXm1014,
        FireMac10, FireTmp, FireMp5, FireUmp45, FireP90, FireGalil, FireFamas, FireAk47,
        FireM4a1, FireScout, FireSg552, FireAug, FireAwp, FireG3sg1, FireSg550, FireM249,
        Count
    };
    inline constexpr const char *kCsSoundPaths[static_cast<int>(CsSound::Count)] = {
        "weapons/c4_beep1.wav", "weapons/c4_beep2.wav",   "weapons/c4_beep3.wav", "weapons/c4_beep4.wav",
        "weapons/c4_beep5.wav", "weapons/c4_plant.wav",   "weapons/c4_disarm.wav", "weapons/c4_disarmed.wav",
        "weapons/c4_explode1.wav", "radio/bombpl.wav",    "radio/bombdef.wav",    "radio/terwin.wav",
        "radio/ctwin.wav",       "player/die1.wav",       "player/die2.wav",      "player/die3.wav",
        "radio/ct_fireinhole.wav", "weapons/flashbang-1.wav", "weapons/flashbang-2.wav",
        "weapons/sg_explode.wav",
        "items/nvg_on.wav", "items/nvg_off.wav", "items/equip_nvg.wav",
        "weapons/ric_metal-1.wav",
        "weapons/m4a1-1.wav", "weapons/usp1.wav",
        "weapons/m4a1_silencer_on.wav", "weapons/m4a1_silencer_off.wav", "weapons/usp_silencer_on.wav",
        "weapons/usp_silencer_off.wav",
        "radio/go.wav", "radio/stormfront.wav", "radio/flankthem.wav", "radio/com_getinpos.wav", "radio/followme.wav",
        "radio/position.wav", "radio/roger.wav", "radio/ct_enemys.wav", "radio/regroup.wav", "radio/takepoint.wav",
        "weapons/glock18-1.wav", "weapons/usp_unsil-1.wav", "weapons/p228-1.wav",  "weapons/deagle-1.wav",
        "weapons/fiveseven-1.wav", "weapons/elite_fire.wav", "weapons/m3-1.wav",   "weapons/xm1014-1.wav",
        "weapons/mac10-1.wav",   "weapons/tmp-1.wav",       "weapons/mp5-1.wav",   "weapons/ump45-1.wav",
        "weapons/p90-1.wav",     "weapons/galil-1.wav",     "weapons/famas-1.wav", "weapons/ak47-1.wav",
        "weapons/m4a1_unsil-1.wav", "weapons/scout_fire-1.wav", "weapons/sg552-1.wav", "weapons/aug-1.wav",
        "weapons/awp1.wav",      "weapons/g3sg1-1.wav",     "weapons/sg550-1.wav", "weapons/m249-1.wav"};
    static_assert(static_cast<int>(CsSound::Count) - static_cast<int>(CsSound::FireGlock) ==
                  static_cast<int>(sizeof(kGuns) / sizeof(kGuns[0])));
    /// The shot of kGuns[gun].
    inline CsSound fireSoundOf(int gun)
    {
        return static_cast<CsSound>(static_cast<int>(CsSound::FireGlock) + gun);
    }
    /// What each gun's shot is in this map (fireSoundOf), found as it is
    /// loaded (Game.arm.cpp); kNoSound: not in it.
    inline std::uint16_t gunSounds[sizeof(kGuns) / sizeof(kGuns[0])] = {};
    /// ... and the M4A1's and the USP's, silenced.
    inline std::uint16_t silencedSounds[2] = {};

    /// Which beep: the first while most of the time is left, the fifth at
    /// the end.
    inline CsSound beepFor(int bombMs)
    {
        const int step = kC4Ms / 5;
        const int left = bombMs < 0 ? 0 : bombMs;
        const int n = 4 - (left >= kC4Ms ? 4 : left / step);
        return static_cast<CsSound>(static_cast<int>(CsSound::Beep1) + (n < 0 ? 0 : (n > 4 ? 4 : n)));
    }

    /// The map's sound for `which` (its name on the card begins with the
    /// sound's stem: soundCardName), or kNoSound if the map has not got it.
    inline std::uint16_t findSound(const LevelView &view, CsSound which)
    {
        char stem[64];
        if (!soundCardStem(kCsSoundPaths[static_cast<int>(which)], stem, sizeof(stem)))
            return kNoSound;
        const std::size_t length = std::strlen(stem);
        for (std::uint32_t i = view.commonSoundCount; i < view.soundCount; ++i)
        {
            const SoundRef ref = view.sound(i);
            if (std::strncmp(ref.name, stem, length) == 0 && ref.name[length] == '.')
                return static_cast<std::uint16_t>(i);
        }
        return kNoSound;
    }

    /// The Counter-Strike view model held for Half-Life weapon `weapon` (its
    /// stand-in): what was bought for its slot -- the knife for the crowbar,
    /// the team's own pistol to begin with -- or the bomb, in hand. nullptr:
    /// the stand-in's own.
    inline const char *viewModelFor(int weapon, const PlayerCs &cs, bool bombInHand)
    {
        if (bombInHand)
            return "v_c4";
        // (A console sent the game in DS mode: a gun of its kind, for some --
        // HlCs.hpp's kGuestViews.)
        const auto viewOf = [](int item) -> const char *
        { return item >= 0 && item < kItemCount ? guestView(kItems[item].view) : nullptr; };
        const char *view = nullptr;
        switch (weapon)
        {
        case kWeaponCrowbar:
            view = "v_knife";
            break;
        case kWeaponGlock:
        case kWeaponPython:
        {
            const char *v = viewOf(cs.pistol);
            view = v != nullptr ? v : (cs.team == kTeamT ? "v_glock18" : "v_usp");
            break;
        }
        case kWeaponShotgun:
        case kWeaponMp5:
        case kWeaponCrossbow:
            return viewOf(cs.primary);
        case kWeaponGrenade:
            switch (grenadeInHand(cs))
            {
            case kGearFlash: view = "v_flashbang"; break;
            case kGearSmoke: view = "v_smokegrenade"; break;
            default: view = "v_hegrenade"; break;
            }
            break;
        default:
            return nullptr;
        }
        // (With the tactical shield: held with it -- not by a console sent
        // the game in DS mode, which has no room for those.)
        if (cs.shield && !guestViews)
            if (const char *withShield = shieldView(view))
                return withShield;
        return view;
    }

    /// The action the BUY button is now (PLANT, DEFUSE), or none.
    enum class Action : std::uint8_t
    {
        Buy,
        Plant,
        Defuse,
    };

    /// The action the button is this frame (the game sets it; the controls
    /// and the touch screen ask it).
    inline Action action = Action::Buy;

    inline int whole(fx v) { return v >> kFxShift; }

    /// The map's own: whether it has a bomb site, and its bomb's radius
    /// (info_map_parameters; 0 the game's).
    inline void mapRules(const LevelView &view, bool &bombMap, int &radius)
    {
        bombMap = false;
        radius = 0;
        for (std::uint32_t i = 0; i < view.logicCount; ++i)
        {
            const LogicRecord &r = view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::BombTarget))
                bombMap = true;
            else if (r.kind == static_cast<std::uint8_t>(LogicKind::MapParameters))
                radius = static_cast<int>(r.extra[0]);
        }
    }

    /// Whether `origin` is in one of the map's bomb sites.
    inline bool inBombSite(const LevelView &view, const fx origin[3])
    {
        for (std::uint32_t i = 0; i < view.logicCount; ++i)
        {
            const LogicRecord &r = view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::BombTarget))
                continue;
            bool inside = true;
            for (int a = 0; a < 3 && inside; ++a)
                inside = origin[a] >= fxFromInt(r.mins[a]) && origin[a] <= fxFromInt(r.maxs[a]);
            if (inside)
                return true;
        }
        return false;
    }

    /// Whether the player is near enough the bomb to defuse it.
    inline bool nearBomb(const Round &r, const fx *origin)
    {
        if (r.phase != Phase::Planted || origin == nullptr)
            return false;
        const int dx = whole(origin[0] - r.bomb[0]), dy = whole(origin[1] - r.bomb[1]), dz = whole(origin[2] - r.bomb[2]);
        if (dx > kDefuseReach || dx < -kDefuseReach || dy > kDefuseReach || dy < -kDefuseReach || dz > 96 || dz < -96)
            return false;
        return dx * dx + dy * dy <= kDefuseReach * kDefuseReach;
    }

    inline Action actionOf(const Round &r, const RoundIn &in)
    {
        if (!in.alive)
            return Action::Buy;
        // (Planting is the bomb's own, in hand: not this button's.)
        if (in.team == kTeamCT && nearBomb(r, in.origin))
            return Action::Defuse;
        return Action::Buy;
    }

    /// Whether buying is open at all (the zone aside): the freeze, and the
    /// buy time after it.
    inline bool buyTime(const Round &r)
    {
        return r.phase == Phase::Freeze || ((r.phase == Phase::Live || r.phase == Phase::Planted) && r.liveMs < kBuyMs);
    }

    /// The loser's money: more for each round lost in a row.
    inline int lossBonus(int lossesInARow)
    {
        const int bonus = kLossFirst + kLossStep * (lossesInARow > 1 ? lossesInARow - 1 : 0);
        return bonus > kLossMost ? kLossMost : bonus;
    }

    inline void addMoney(PlayerCs &cs, int amount)
    {
        cs.money += amount;
        cs.money = cs.money > kMoneyMost ? kMoneyMost : (cs.money < 0 ? 0 : cs.money);
    }

    /// The round won by `winner` for `why`: the score, and the money -- the
    /// winners' for how they won, the losers' for how often they have lost
    /// (and the terrorists' for a bomb that was planted).
    inline void endRound(Round &r, PlayerCs &cs, std::uint8_t winner, Why why)
    {
        r.phase = Phase::Over;
        r.phaseMs = 0;
        r.winner = winner;
        r.why = why;
        r.plantMs = r.defuseMs = 0;
        if (winner == kTeamT)
        {
            ++r.scoreT;
            r.lossesT = 0;
            ++r.lossesCT;
        }
        else
        {
            ++r.scoreCT;
            r.lossesCT = 0;
            ++r.lossesT;
        }
        const bool objective = why == Why::TargetBombed || why == Why::BombDefused;
        if (cs.team == winner)
            addMoney(cs, objective ? kWinObjective : kWinElimination);
        else
        {
            const int losses = cs.team == kTeamT ? r.lossesT : r.lossesCT;
            addMoney(cs, lossBonus(losses) + (cs.team == kTeamT && r.plantedThisRound ? kPlantedLossBonus : 0));
        }
    }

    /// Integer square root, for the blast's distance (once a bomb).
    inline int isqrt(std::uint32_t v)
    {
        std::uint32_t r = 0, bit = 1u << 30;
        while (bit > v)
            bit >>= 2;
        while (bit != 0)
        {
            if (v >= r + bit)
            {
                v -= r + bit;
                r = (r >> 1) + bit;
            }
            else
                r >>= 1;
            bit >>= 2;
        }
        return static_cast<int>(r);
    }

    /// What the bomb does to someone `distance` units from it, as 1.6's
    /// RadiusDamage has it for the bomb: the radius's worth at its middle,
    /// falling off evenly to nothing at three and a half times the radius,
    /// through walls.
    inline int blastAt(int radius, int distance)
    {
        const int reach = radius * 7 / 2;
        if (distance >= reach)
            return 0;
        return static_cast<int>(static_cast<std::int64_t>(radius) * (reach - distance) / reach);
    }

    /// A frame of the round.
    inline RoundOut roundTick(Round &r, const RoundIn &in, PlayerCs &cs)
    {
        RoundOut out;
        r.phaseMs += in.ms;
        switch (r.phase)
        {
        case Phase::Freeze:
            if (r.phaseMs >= kFreezeMs)
            {
                r.phase = Phase::Live;
                r.phaseMs = 0;
                r.liveMs = 0;
                r.clockMs = r.roundMs;
                out.unfrozen = true;
            }
            return out;
        case Phase::Over:
            if (r.phaseMs >= kRoundOverMs)
            {
                // The next round: frozen, the bomb to the terrorists again.
                r.phase = Phase::Freeze;
                r.phaseMs = 0;
                r.liveMs = 0;
                ++r.number;
                r.plantedThisRound = false;
                r.hasBomb = r.bombMap && cs.team == kTeamT;
                r.botBomb = -1;
                r.why = Why::None;
                r.winner = 0;
                c4InHand = false;
                out.started = true;
            }
            return out;
        case Phase::Live:
        case Phase::Planted:
            break;
        }

        r.liveMs += in.ms;
        const std::uint8_t mine = in.team, theirs = in.team == kTeamT ? kTeamCT : kTeamT;

        // The bomb, while it is down: its beeps, faster as it goes on; it
        // goes off; or it is defused.
        if (r.phase == Phase::Planted)
        {
            r.bombMs -= in.ms;
            r.beepMs -= in.ms;
            if (r.beepMs <= 0)
            {
                out.beep = true;
                r.beepMs = r.bombMs > 20000 ? 1500 : r.bombMs > 10000 ? 1000 : r.bombMs > 5000 ? 500 : 250;
            }
            if (in.team == kTeamCT && in.alive && in.actionHeld && nearBomb(r, in.origin))
            {
                r.defuseMs += in.ms;
                if (r.defuseMs >= (in.hasKit ? kDefuseKitMs : kDefuseMs))
                {
                    out.defused = out.ended = true;
                    endRound(r, cs, kTeamCT, Why::BombDefused);
                    return out;
                }
            }
            else
                r.defuseMs = 0;
            if (r.bombMs <= 0)
            {
                out.exploded = out.ended = true;
                if (in.alive && in.origin != nullptr)
                {
                    const int dx = whole(in.origin[0] - r.bomb[0]), dy = whole(in.origin[1] - r.bomb[1]),
                              dz = whole(in.origin[2] - r.bomb[2]);
                    const std::uint32_t d2 = static_cast<std::uint32_t>(dx * dx + dy * dy + dz * dz);
                    out.blast = blastAt(r.bombRadius, isqrt(d2));
                }
                endRound(r, cs, kTeamT, Why::TargetBombed);
                return out;
            }
        }

        // Planting: held in a bomb site, still alive.
        if (r.phase == Phase::Live && r.hasBomb && in.team == kTeamT && in.alive && in.inBombSite && in.actionHeld &&
            in.origin != nullptr)
        {
            r.plantMs += in.ms;
            if (r.plantMs >= kPlantMs)
            {
                r.phase = Phase::Planted;
                r.phaseMs = 0;
                r.hasBomb = false;
                r.plantedThisRound = true;
                r.bombMs = kC4Ms;
                r.beepMs = 0;
                r.plantMs = 0;
                // (Where a standing player's middle would be, 36 over the
                // floor, as a bot's is put: crouched, theirs is 18 over it.)
                for (int a = 0; a < 3; ++a)
                    r.bomb[a] = in.origin[a];
                r.bomb[2] += fxFromInt(in.ducked ? 18 : 0);
                out.planted = true;
            }
        }
        else
            r.plantMs = 0;

        // A side with everyone on it dead. (A side with nobody on it at all
        // is not beaten: the player alone, with no bots.)
        const int mineAlive = (in.alive ? 1 : 0) + in.matesAlive;
        const int theirsAlive = in.enemiesAlive;
        if (mineAlive == 0)
        {
            // (Terrorists all dead with the bomb down: it may still go off.)
            if (!(mine == kTeamT && r.phase == Phase::Planted))
            {
                out.ended = true;
                endRound(r, cs, theirs, theirs == kTeamT ? Why::CtsDead : Why::TerroristsDead);
                return out;
            }
        }
        if (in.enemiesPlaying > 0 && theirsAlive == 0 && !(theirs == kTeamT && r.phase == Phase::Planted))
        {
            out.ended = true;
            endRound(r, cs, mine, mine == kTeamT ? Why::CtsDead : Why::TerroristsDead);
            return out;
        }

        // The round's clock (not once the bomb is down: then it is the bomb's).
        if (r.phase == Phase::Live)
        {
            r.clockMs -= in.ms;
            if (r.clockMs <= 0)
            {
                r.clockMs = 0;
                out.ended = true;
                if (r.bombMap)
                    endRound(r, cs, kTeamCT, Why::TargetSaved);
                else
                    endRound(r, cs, kTeamT, Why::HostagesNotSaved);
            }
        }
        return out;
    }

    /// What the player carried out of a round, for the next.
    struct Carry
    {
        bool valid = false;  ///< a round is being started again (not a new game)
        bool survived = false;
        Inventory inv;
        Arsenal arsenal;
        int primary = kNoItem, pistol = kNoItem;
        bool helmet = false, defuser = false, nvg = false, shield = false;
    };
    inline Carry carry;

    inline void carryOut(Carry &c, const PlayerState &state, const Arsenal &arsenal, const PlayerCs &cs)
    {
        c.valid = true;
        c.survived = state.health > 0;
        c.inv = state.inv;
        c.arsenal = arsenal;
        c.primary = cs.primary;
        c.pistol = cs.pistol;
        c.helmet = cs.helmet;
        c.defuser = cs.defuser;
        c.nvg = cs.nvg;
        c.shield = cs.shield;
    }

    /// A player into a new round: what they carried, if they lived; a
    /// pistol, if they died. Their money and team are kept either way.
    inline void newRoundLoadout(PlayerState &state, PlayerCs &cs, const Carry &c)
    {
        const int money = cs.money;
        const std::uint8_t team = cs.team, faction = cs.faction;
        if (c.survived)
        {
            state.inv = c.inv;
            if (state.arsenal != nullptr)
                *state.arsenal = c.arsenal;
            cs.primary = c.primary;
            cs.pistol = c.pistol;
            cs.helmet = c.helmet;
            cs.defuser = c.defuser;
            cs.nvg = c.nvg;
            cs.shield = c.shield;
            cs.shieldUp = false;
        }
        else
        {
            Setup s;
            s.team = team;
            s.faction = faction;
            startLoadout(state, cs, s);
        }
        cs.money = money;
        cs.team = team;
        cs.faction = faction;
        state.health = 100;
    }

    /// The two lines the touch screen shows of the round: what is happening
    /// (and its clock), and the score.
    inline void roundText(const Round &r, std::uint8_t team, char *line1, std::size_t size1, char *line2,
                          std::size_t size2)
    {
        const auto clock = [](int ms, char *out, std::size_t size)
        {
            const int s = ms <= 0 ? 0 : (ms + 999) / 1000;
            std::snprintf(out, size, "%d:%02d", s / 60, s % 60);
        };
        char t[8];
        switch (r.phase)
        {
        case Phase::Freeze:
            clock(kFreezeMs - r.phaseMs, t, sizeof(t));
            std::snprintf(line1, size1, "BUY TIME  %s", t);
            break;
        case Phase::Live:
            clock(r.clockMs, t, sizeof(t));
            std::snprintf(line1, size1, "%s%s", t,
                          !(r.hasBomb && team == kTeamT) ? ""
                          : c4InHand                     ? "  HOLD FIRE IN A SITE"
                                                         : "  BOMB: SLOT 5");
            break;
        case Phase::Planted:
            clock(r.bombMs, t, sizeof(t));
            std::snprintf(line1, size1, "BOMB PLANTED  %s", t);
            break;
        case Phase::Over:
        {
            const char *why = r.why == Why::TargetBombed  ? "TARGET BOMBED"
                              : r.why == Why::BombDefused ? "BOMB DEFUSED"
                              : r.why == Why::TargetSaved ? "TARGET SAVED"
                                                          : (r.winner == kTeamT ? "TERRORISTS WIN" : "CTS WIN");
            std::snprintf(line1, size1, "%s", why);
            break;
        }
        }
        std::snprintf(line2, size2, "CT %d  -  T %d", r.scoreCT, r.scoreT);
    }

    /// How far a plant or a defuse has got, of 256 (0: none going).
    inline int actionProgress(const Round &r, bool hasKit)
    {
        if (r.plantMs > 0)
            return r.plantMs * 256 / kPlantMs;
        if (r.defuseMs > 0)
            return r.defuseMs * 256 / (hasKit ? kDefuseKitMs : kDefuseMs);
        return 0;
    }
} // namespace hldata::cs
