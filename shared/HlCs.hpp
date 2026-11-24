#pragma once

// Counter-Strike's own rules, as far as they have come: the game set up
// before it starts (which map, how many bots and how good, which team, how
// much money), the player's money and team, where they may buy, and the buy
// menu on the touch screen -- what it offers, what each costs and who may
// have it, and where its buttons are.
//
// Its weapons are not here yet. Until they are, each of Counter-Strike's
// stands in as the nearest of Half-Life's (Item::standIn): the rifles and the
// sub-machine guns as the MP5, the sniper rifles as the crossbow and its
// sight, the Desert Eagle as the .357, the pump guns as the shotgun, the HE
// grenade as the hand grenade. The knife is the crowbar. What is bought is
// Counter-Strike's own (its price, its team, its slot); only how it shoots
// is Half-Life's.
//
// std-only and inline: the host tests run it (tests/test_cs.cpp); the
// console's code draws it (render/BottomScreen) and asks it (game/Input,
// game/Player, ui/Menu).

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "HlGame.hpp"
#include "HlLevel.hpp"
#include "HlLevelReader.hpp"
#include "HlPlayerMove.hpp"
#include "HlTouchLayout.hpp"

namespace hldata::cs
{
    // ------------------------------------------------------------- teams

    enum Team : std::uint8_t
    {
        kTeamT = 1,  ///< the terrorists: they start at info_player_deathmatch
        kTeamCT = 2, ///< the counter-terrorists: at info_player_start
    };
    /// A bit per team, for who may buy what.
    inline constexpr std::uint8_t kTeamsT = 1u << kTeamT;
    inline constexpr std::uint8_t kTeamsCT = 1u << kTeamCT;
    inline constexpr std::uint8_t kTeamsBoth = kTeamsT | kTeamsCT;

    // ------------------------------------------------------------- the setup

    /// The game as NEW GAME sets it up (the setup screen, ui/Menu).
    inline constexpr int kStartMoney[] = {800, 1600, 2400, 4000, 8000, 16000};
    inline constexpr int kStartMoneyCount = static_cast<int>(sizeof(kStartMoney) / sizeof(kStartMoney[0]));
    inline constexpr int kMoneyMost = 16000;
    inline constexpr int kBotSkills = 4;
    inline constexpr const char *kBotSkillNames[kBotSkills] = {"easy", "normal", "hard", "expert"};
    inline constexpr int kRoundMinutesLeast = 1;
    inline constexpr int kRoundMinutesMost = 9;
    inline constexpr int kMapNameMost = 15; ///< a map's name, without its end

    struct Setup
    {
        char map[kMapNameMost + 1] = {}; ///< empty: the game's start (de_dust2)
        std::uint8_t tBots = 2;          ///< terrorist bots, 0..kBotSlotsPerSide
        std::uint8_t ctBots = 2;         ///< counter-terrorist bots, likewise
        std::uint8_t botSkill = 1;       ///< into kBotSkillNames
        std::uint8_t team = kTeamCT;
        std::uint8_t money = 0;          ///< into kStartMoney
        std::uint8_t roundMinutes = 2;   ///< mp_roundtime
        std::uint8_t faction = 0;        ///< of the team's four (kFactionNames)
        std::uint8_t match = 3;          ///< into kMatchRounds: how long the game is
    };

    /// How long a game is: the rounds a team may win before the next is its
    /// victory -- 13: the fourteenth won wins it, and 13-13 is a draw (all
    /// 26 played). 0: on until the player leaves.
    inline constexpr int kMatchRounds[] = {0, 5, 10, 13, 15};
    inline constexpr int kMatchChoices = static_cast<int>(sizeof(kMatchRounds) / sizeof(kMatchRounds[0]));
    inline void nextMatch(Setup &s) { s.match = static_cast<std::uint8_t>((s.match + 1) % kMatchChoices); }
    inline int matchRounds(const Setup &s) { return kMatchRounds[s.match < kMatchChoices ? s.match : 0]; }

    /// How the game stands, by the score: on, or won by a side, or drawn.
    enum class MatchEnd : std::uint8_t
    {
        On,
        TerroristsWin,
        CounterTerroristsWin,
        Draw,
    };
    inline MatchEnd matchEnd(int scoreT, int scoreCT, int rounds)
    {
        if (rounds <= 0)
            return MatchEnd::On;
        if (scoreT > rounds)
            return MatchEnd::TerroristsWin;
        if (scoreCT > rounds)
            return MatchEnd::CounterTerroristsWin;
        if (scoreT == rounds && scoreCT == rounds)
            return MatchEnd::Draw;
        return MatchEnd::On;
    }

    /// Each team's four factions, as the game's class menu offers them, and
    /// their player models (models/player/<name>/<name>.mdl).
    inline constexpr int kFactions = 4;
    inline constexpr const char *kFactionNamesT[kFactions] = {"Phoenix Connexion", "Elite Crew", "Arctic Avengers",
                                                              "Guerilla Warfare"};
    inline constexpr const char *kFactionNamesCT[kFactions] = {"Seal Team 6", "GSG-9", "SAS", "GIGN"};
    inline constexpr const char *kFactionModelsT[kFactions] = {"terror", "leet", "arctic", "guerilla"};
    inline constexpr const char *kFactionModelsCT[kFactions] = {"urban", "gsg9", "sas", "gign"};
    inline const char *factionName(std::uint8_t team, int faction)
    {
        return (team == 1 ? kFactionNamesT : kFactionNamesCT)[faction >= 0 && faction < kFactions ? faction : 0];
    }
    inline const char *factionModel(std::uint8_t team, int faction)
    {
        return (team == 1 ? kFactionModelsT : kFactionModelsCT)[faction >= 0 && faction < kFactions ? faction : 0];
    }

    /// What the setup screen last had, and the game that is being played's.
    inline Setup setup;

    inline int startMoney(const Setup &s)
    {
        return kStartMoney[s.money < kStartMoneyCount ? s.money : 0];
    }
    // The setup screen's rows each step their value round.
    inline constexpr int kBotSlotsPerSide = 5; ///< bots a team has at most (one at each start but the first)
    inline std::uint8_t nextBotCount(std::uint8_t n)
    {
        return static_cast<std::uint8_t>(n >= kBotSlotsPerSide ? 0 : n + 1);
    }
    inline void nextTBots(Setup &s) { s.tBots = nextBotCount(s.tBots); }
    inline void nextCTBots(Setup &s) { s.ctBots = nextBotCount(s.ctBots); }
    inline void nextBotSkill(Setup &s) { s.botSkill = static_cast<std::uint8_t>((s.botSkill + 1) % kBotSkills); }
    inline void nextTeam(Setup &s) { s.team = s.team == kTeamCT ? kTeamT : kTeamCT; }
    inline void nextFaction(Setup &s) { s.faction = static_cast<std::uint8_t>((s.faction + 1) % kFactions); }
    inline void nextMoney(Setup &s) { s.money = static_cast<std::uint8_t>((s.money + 1) % kStartMoneyCount); }
    inline void nextRoundMinutes(Setup &s)
    {
        s.roundMinutes = static_cast<std::uint8_t>(s.roundMinutes >= kRoundMinutesMost ? kRoundMinutesLeast
                                                                                         : s.roundMinutes + 1);
    }
    inline void setMap(Setup &s, const char *name)
    {
        std::size_t n = 0;
        for (; name != nullptr && name[n] != '\0' && n < static_cast<std::size_t>(kMapNameMost); ++n)
            s.map[n] = name[n];
        for (; n <= static_cast<std::size_t>(kMapNameMost); ++n)
            s.map[n] = '\0';
    }
    /// Of `maps` (count of them, each a name), the one after the setup's --
    /// the first if it is the last, or not among them.
    inline const char *nextMap(const Setup &s, const char *const *maps, int count)
    {
        if (count <= 0)
            return "";
        for (int i = 0; i < count; ++i)
        {
            if (std::strcmp(maps[i], s.map) == 0)
                return maps[(i + 1) % count];
        }
        return maps[0];
    }

    /// The setup as a file (data/cstrike/setup.dat): "CSSU", a version, the
    /// fields, the map's name. A file of another version, or damaged, is
    /// read as nothing (the setup as it was).
    inline constexpr char kSetupMagic[4] = {'C', 'S', 'S', 'U'};
    inline constexpr std::uint8_t kSetupVersion = 5; // (5: rounds of 2 minutes, by default)
    inline constexpr std::size_t kSetupBytes = 4 + 1 + 8 + kMapNameMost + 1;

    inline std::size_t writeSetup(const Setup &s, std::uint8_t (&out)[kSetupBytes])
    {
        std::memcpy(out, kSetupMagic, 4);
        out[4] = kSetupVersion;
        out[5] = s.tBots;
        out[6] = s.botSkill;
        out[7] = s.team;
        out[8] = s.money;
        out[9] = s.roundMinutes;
        out[10] = s.faction;
        out[11] = s.ctBots;
        out[12] = s.match;
        std::memcpy(out + 13, s.map, kMapNameMost + 1);
        out[kSetupBytes - 1] = 0;
        return kSetupBytes;
    }

    inline bool readSetup(const std::uint8_t *bytes, std::size_t size, Setup &out)
    {
        if (bytes == nullptr || size != kSetupBytes || std::memcmp(bytes, kSetupMagic, 4) != 0 ||
            bytes[4] != kSetupVersion)
            return false;
        Setup s;
        s.tBots = bytes[5] <= kBotSlotsPerSide ? bytes[5] : s.tBots;
        s.ctBots = bytes[11] <= kBotSlotsPerSide ? bytes[11] : s.ctBots;
        s.botSkill = bytes[6] < kBotSkills ? bytes[6] : s.botSkill;
        s.team = bytes[7] == kTeamT ? kTeamT : kTeamCT;
        s.money = bytes[8] < kStartMoneyCount ? bytes[8] : 0;
        s.roundMinutes = bytes[9] >= kRoundMinutesLeast && bytes[9] <= kRoundMinutesMost ? bytes[9] : s.roundMinutes;
        s.faction = bytes[10] < kFactions ? bytes[10] : 0;
        s.match = bytes[12] < kMatchChoices ? bytes[12] : s.match;
        // (Only letters, digits and _ in a map's name: anything else ends it.)
        for (int i = 0; i < kMapNameMost; ++i)
        {
            const char c = static_cast<char>(bytes[13 + i]);
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok)
                break;
            s.map[i] = c;
        }
        out = s;
        return true;
    }

    // ------------------------------------------------------------- the bots

    // Until Counter-Strike's own bots are written, Half-Life's people stand
    // in for them (as its weapons do for the guns): the other team's are its
    // soldiers -- who hunt, take cover, throw grenades -- and the player's own
    // team's its security guards, who are the soldiers' enemies too. Each map
    // has a monstermaker for each kind at each team start but the first (the
    // player's: teamStart's), named botMakerName; the round wakes as many as
    // the setup asks for, on the right sides, at each start.

    /// "csbot_<t|ct>_<s|g>_<n>": the maker of a soldier (s) or a guard (g) at
    /// the n-th start (1..kBotSlotsPerSide) of team `side`'s.
    inline void botMakerName(char *out, std::size_t size, std::uint8_t side, bool soldier, int n)
    {
        std::snprintf(out, size, "csbot_%s_%c_%d", side == kTeamT ? "t" : "ct", soldier ? 's' : 'g', n);
    }

    /// The bots awake this round: their actors, the other side's [0] and the
    /// player's own [1].
    struct BotRoster
    {
        int slot[2][kBotSlotsPerSide] = {};
        int count[2] = {0, 0};
    };
    inline BotRoster roster;

    /// The match's scoreboard: each one's kills and deaths, by side (as the
    /// roster's: 0 the player's enemies, 1 the player's own), and the
    /// player's. Who last hurt each (a bot's actor, or kAiPlayer -2; -1:
    /// nobody), whose a death is.
    struct Score
    {
        std::int16_t kills = 0, deaths = 0;
    };
    struct Scoreboard
    {
        Score bot[2][kBotSlotsPerSide];
        Score player;
        std::int16_t hitBy[256] = {};
        bool playerUp = true;
        constexpr Scoreboard()
        {
            for (std::int16_t &h : hitBy)
                h = -1;
        }
    };
    // (constinit: made as the program is, not by code at load -- a family,
    // which includes this, may run none: tools/dsl_family.py.)
    constinit inline Scoreboard scores;
    /// Each bot's name, by its team and its place in it (made up for this
    /// port: short, to fit the touch screen's rows).
    inline constexpr const char *kBotNames[2][kBotSlotsPerSide] = {
        {"Vex", "Kestrel", "Dune", "Rook", "Sable"},   // the terrorists'
        {"Hawk", "Mercer", "Juno", "Ridge", "Talon"}}; // the counter-terrorists'
    inline const char *botName(std::uint8_t team, int n)
    {
        return n >= 0 && n < kBotSlotsPerSide ? kBotNames[team == kTeamT ? 0 : 1][n] : "Bot";
    }
    /// The bot the player, shot down, is watching (Player.arm.cpp: the
    /// view follows it); -1: none, the player's own view.
    inline int spectating = -1;

    /// The bots' guns (HlCsBots.hpp has what they cost and do).
    enum BotGun : std::uint8_t
    {
        kGunGlock, ///< the terrorists' pistol
        kGunUsp,   ///< the counter-terrorists'
        kGunAk47,  ///< the terrorists' rifle
        kGunM4a1,  ///< the counter-terrorists'
        kGunShield, ///< the counter-terrorists' tactical shield, with the USP
        kBotGuns
    };
    /// Each as it is seen held (models/p_*.mdl: the converter cooks them
    /// into a Counter-Strike map).
    inline constexpr const char *kBotGunModels[kBotGuns] = {"p_glock18", "p_usp", "p_ak47", "p_m4a1",
                                                            "shield/p_shield_usp"};
    /// Which gun each actor that is a bot has, + 1 (0: not a bot), the
    /// actors' first kBotGunActors: for the picture (ModelDraw) and the shots.
    inline constexpr int kBotGunActors = 256;
    inline std::uint8_t botGunHeld[kBotGunActors] = {};
    /// ... and each bot's team, plus one (0: not a bot), for whose grenade
    /// hurts whom (HlCsBots.hpp's heGoesOff).
    inline std::uint8_t botTeamOf[kBotGunActors] = {};
    /// The ms each bot of the other side stays on the player's radar: seen
    /// by one of the player's side, or heard shooting (HlCsBots.hpp). 0:
    /// not on it.
    inline std::uint16_t spottedMs[kBotGunActors] = {};

    /// The setup's bots by side, as the game wakes them: the other side's
    /// (enemies) and the player's own (mates), each as many as the setup's
    /// row for that team says.
    inline void botSides(const Setup &setup, std::uint8_t playerTeam, int &enemies, int &mates)
    {
        const int t = setup.tBots > kBotSlotsPerSide ? kBotSlotsPerSide : setup.tBots;
        const int ct = setup.ctBots > kBotSlotsPerSide ? kBotSlotsPerSide : setup.ctBots;
        enemies = playerTeam == kTeamT ? ct : t;
        mates = playerTeam == kTeamT ? t : ct;
    }

    // ------------------------------------------------------------- what is sold

    enum Menu : std::uint8_t
    {
        kMenuPistols,
        kMenuShotguns,
        kMenuSmgs,
        kMenuRifles,
        kMenuMachineGun,
        kMenuGear,
        kMenuCount
    };
    inline constexpr const char *kMenuNames[kMenuCount] = {"PISTOLS", "SHOTGUNS", "SMG",
                                                           "RIFLES",  "MACHINE GUNS", "EQUIPMENT"};

    enum Slot : std::uint8_t
    {
        kSlotPrimary,
        kSlotPistol,
        kSlotGear,
    };

    enum Gear : std::uint8_t
    {
        kGearNone,
        kGearKevlar,
        kGearKevlarHelmet,
        kGearHe,
        kGearDefuser,
        kGearFlash,
        kGearSmoke,
        kGearNvg,  ///< the night vision goggles
        kGearShield, ///< the tactical shield (a counter-terrorist's, in place of a primary)
    };

    struct Item
    {
        const char *name;
        std::uint16_t price;
        std::uint8_t menu;   ///< Menu
        std::uint8_t teams;  ///< kTeams*: who may buy it
        std::uint8_t slot;   ///< Slot
        std::uint8_t standIn; ///< the Half-Life weapon it is for now (kWeapon*), or kWeaponNone
        std::uint8_t gear;   ///< Gear, for what is not a weapon
        const char *view = nullptr; ///< its own view model (CsView), held in place of the stand-in's
        const char *label = nullptr; ///< as 1.6's buy menu names it (its cstrike_english.txt)
    };

    /// Counter-Strike 1.6's buy menu: its prices, and who may buy what.
    inline constexpr Item kItems[] = {
        // Pistols.
        {"GLOCK 18", 400, kMenuPistols, kTeamsBoth, kSlotPistol, kWeaponGlock, kGearNone, "v_glock18", "9X19MM SIDEARM"},
        {"USP", 500, kMenuPistols, kTeamsBoth, kSlotPistol, kWeaponGlock, kGearNone, "v_usp", "KM .45 TACTICAL"},
        {"P228", 600, kMenuPistols, kTeamsBoth, kSlotPistol, kWeaponGlock, kGearNone, "v_p228", "228 COMPACT"},
        {"DESERT EAGLE", 650, kMenuPistols, kTeamsBoth, kSlotPistol, kWeaponPython, kGearNone, "v_deagle", "NIGHT HAWK .50C"},
        {"FIVE-SEVEN", 750, kMenuPistols, kTeamsCT, kSlotPistol, kWeaponGlock, kGearNone, "v_fiveseven", "ES FIVE-SEVEN"},
        {"DUAL ELITES", 800, kMenuPistols, kTeamsT, kSlotPistol, kWeaponGlock, kGearNone, "v_elite", ".40 DUAL ELITES"},
        // Shotguns.
        {"M3", 1700, kMenuShotguns, kTeamsBoth, kSlotPrimary, kWeaponShotgun, kGearNone, "v_m3", "LEONE 12 GAUGE SUPER"},
        {"XM1014", 3000, kMenuShotguns, kTeamsBoth, kSlotPrimary, kWeaponShotgun, kGearNone, "v_xm1014", "LEONE YG1265 AUTO SHOTGUN"},
        // Sub-machine guns.
        {"MAC-10", 1400, kMenuSmgs, kTeamsT, kSlotPrimary, kWeaponMp5, kGearNone, "v_mac10", "INGRAM MAC-10"},
        {"TMP", 1250, kMenuSmgs, kTeamsCT, kSlotPrimary, kWeaponMp5, kGearNone, "v_tmp", "SCHMIDT MACHINE PISTOL"},
        {"MP5", 1500, kMenuSmgs, kTeamsBoth, kSlotPrimary, kWeaponMp5, kGearNone, "v_mp5", "KM SUB-MACHINE GUN"},
        {"UMP45", 1700, kMenuSmgs, kTeamsBoth, kSlotPrimary, kWeaponMp5, kGearNone, "v_ump45", "KM UMP45"},
        {"P90", 2350, kMenuSmgs, kTeamsBoth, kSlotPrimary, kWeaponMp5, kGearNone, "v_p90", "ES C90"},
        // Rifles.
        {"GALIL", 2000, kMenuRifles, kTeamsT, kSlotPrimary, kWeaponMp5, kGearNone, "v_galil", "IDF DEFENDER"},
        {"FAMAS", 2250, kMenuRifles, kTeamsCT, kSlotPrimary, kWeaponMp5, kGearNone, "v_famas", "CLARION 5.56"},
        {"AK-47", 2500, kMenuRifles, kTeamsT, kSlotPrimary, kWeaponMp5, kGearNone, "v_ak47", "CV-47"},
        {"M4A1", 3100, kMenuRifles, kTeamsCT, kSlotPrimary, kWeaponMp5, kGearNone, "v_m4a1", "MAVERICK M4A1 CARBINE"},
        {"SCOUT", 2750, kMenuRifles, kTeamsBoth, kSlotPrimary, kWeaponCrossbow, kGearNone, "v_scout", "SCHMIDT SCOUT"},
        {"SG 552", 3500, kMenuRifles, kTeamsT, kSlotPrimary, kWeaponMp5, kGearNone, "v_sg552", "KRIEG 552"},
        {"AUG", 3500, kMenuRifles, kTeamsCT, kSlotPrimary, kWeaponMp5, kGearNone, "v_aug", "BULLPUP"},
        {"AWP", 4750, kMenuRifles, kTeamsBoth, kSlotPrimary, kWeaponCrossbow, kGearNone, "v_awp", "MAGNUM SNIPER RIFLE"},
        {"G3SG1", 5000, kMenuRifles, kTeamsT, kSlotPrimary, kWeaponCrossbow, kGearNone, "v_g3sg1", "D3/AU-1"},
        {"SG 550", 4200, kMenuRifles, kTeamsCT, kSlotPrimary, kWeaponCrossbow, kGearNone, "v_sg550", "KRIEG 550 COMMANDO"},
        // The machine gun.
        {"M249", 5750, kMenuMachineGun, kTeamsBoth, kSlotPrimary, kWeaponMp5, kGearNone, "v_m249", "M249"},
        // Equipment.
        {"KEVLAR", 650, kMenuGear, kTeamsBoth, kSlotGear, kWeaponNone, kGearKevlar, nullptr, "KEVLAR"},
        {"KEVLAR+HELMET", 1000, kMenuGear, kTeamsBoth, kSlotGear, kWeaponNone, kGearKevlarHelmet, nullptr, "KEVLAR+HELMET"},
        {"HE GRENADE", 300, kMenuGear, kTeamsBoth, kSlotGear, kWeaponGrenade, kGearHe, "v_hegrenade", "HE GRENADE"},
        {"FLASHBANG", 200, kMenuGear, kTeamsBoth, kSlotGear, kWeaponGrenade, kGearFlash, "v_flashbang", "FLASHBANG"},
        {"DEFUSE KIT", 200, kMenuGear, kTeamsCT, kSlotGear, kWeaponNone, kGearDefuser, nullptr, "DEFUSAL KIT"},
        {"SMOKE GRENADE", 300, kMenuGear, kTeamsBoth, kSlotGear, kWeaponGrenade, kGearSmoke, "v_smokegrenade", "SMOKE GRENADE"},
        {"NIGHTVISION", 1250, kMenuGear, kTeamsBoth, kSlotGear, kWeaponNone, kGearNvg, nullptr, "NIGHTVISION"},
        {"SHIELD", 2200, kMenuGear, kTeamsCT, kSlotGear, kWeaponNone, kGearShield, nullptr, "TACTICAL SHIELD"},
    };
    inline constexpr int kItemCount = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));
    inline constexpr int kNoItem = -1;

    /// Each gun's numbers as Counter-Strike 1.6 has them: what a round does
    /// to an unarmoured body (a pellet, for a shotgun), how long between
    /// shots, the magazine and what is carried besides, the reload, the
    /// pellets, how far a round may stray (in 4096ths, the stand-in's code's
    /// cone: the first shots', standing), and how much of armour a round goes
    /// through (in hundredths: the game's armour-ratio modifier); how fast
    /// its holder runs (the game's GetMaxSpeed, units a second), and with
    /// its sight up (0: as without); and what is left of a round's damage
    /// each 500 units it goes (in thousandths: its RANGE_MODIFER; the pump
    /// guns' pellets keep theirs).
    struct CsGun
    {
        const char *item;
        int damage, refireMs, clip, reserve, reloadMs, pellets, cone, armorPierce, speed, zoomSpeed, rangeMod;
        std::uint8_t ammo; ///< CsAmmo: what it fires, as it is sold
    };
    /// The rounds 1.6 sells, a box at a time: how many to a box, and what
    /// it costs (each gun carries as many as its `reserve` besides its
    /// magazine).
    enum CsAmmo : std::uint8_t
    {
        kAmmo9mm, kAmmo45acp, kAmmo357sig, kAmmo50ae, kAmmo57mm, kAmmoBuck, kAmmo556, kAmmo762, kAmmo338, kAmmo556box,
    };
    inline constexpr struct
    {
        int amount, price;
    } kAmmoBoxes[] = {{30, 20}, {12, 25}, {13, 50}, {7, 40}, {50, 50}, {8, 65}, {30, 60}, {30, 80}, {10, 125}, {30, 60}};

    inline constexpr CsGun kGuns[] = {
        {"GLOCK 18", 25, 150, 20, 120, 2200, 1, 82, 105, 250, 0, 750, kAmmo9mm},
        {"USP", 34, 150, 12, 100, 2700, 1, 61, 100, 250, 0, 790, kAmmo45acp},
        {"P228", 32, 150, 13, 52, 2700, 1, 72, 125, 250, 0, 800, kAmmo357sig},
        {"DESERT EAGLE", 54, 225, 7, 35, 2200, 1, 72, 150, 250, 0, 810, kAmmo50ae},
        {"FIVE-SEVEN", 20, 150, 20, 100, 2700, 1, 72, 150, 250, 0, 885, kAmmo57mm},
        {"DUAL ELITES", 36, 125, 30, 120, 4500, 1, 92, 105, 250, 0, 750, kAmmo9mm},
        {"M3", 20, 880, 8, 32, 500, 9, 357, 100, 230, 0, 1000, kAmmoBuck},
        {"XM1014", 20, 250, 7, 32, 500, 6, 357, 100, 240, 0, 1000, kAmmoBuck},
        {"MAC-10", 29, 70, 30, 100, 3150, 1, 123, 95, 250, 0, 820, kAmmo45acp},
        {"TMP", 20, 70, 30, 120, 2100, 1, 102, 100, 250, 0, 850, kAmmo9mm},
        {"MP5", 26, 75, 30, 120, 2630, 1, 92, 100, 250, 0, 840, kAmmo9mm},
        {"UMP45", 30, 100, 25, 100, 3500, 1, 92, 100, 250, 0, 820, kAmmo45acp},
        {"P90", 21, 66, 50, 100, 3400, 1, 102, 150, 245, 0, 885, kAmmo57mm},
        {"GALIL", 30, 90, 35, 90, 2450, 1, 72, 155, 240, 0, 980, kAmmo556},
        {"FAMAS", 30, 90, 25, 90, 3300, 1, 61, 140, 240, 0, 960, kAmmo556},
        {"AK-47", 36, 100, 30, 90, 2450, 1, 72, 155, 221, 0, 980, kAmmo762},
        {"M4A1", 32, 90, 30, 90, 3050, 1, 61, 140, 230, 0, 970, kAmmo556},
        {"SCOUT", 75, 1250, 10, 90, 2000, 1, 0, 170, 260, 220, 980, kAmmo762},
        {"SG 552", 33, 90, 30, 90, 3000, 1, 61, 140, 235, 0, 955, kAmmo556},
        {"AUG", 32, 90, 30, 90, 3300, 1, 61, 140, 240, 0, 960, kAmmo556},
        {"AWP", 115, 1450, 10, 30, 2500, 1, 0, 195, 210, 150, 990, kAmmo338},
        {"G3SG1", 80, 250, 20, 90, 3500, 1, 20, 165, 210, 150, 980, kAmmo762},
        {"SG 550", 70, 250, 30, 90, 3350, 1, 20, 145, 210, 150, 980, kAmmo556},
        {"M249", 32, 100, 100, 200, 4700, 1, 102, 150, 220, 0, 970, kAmmo556box},
    };
    /// Item `item`'s numbers, if it is a gun.
    inline const CsGun *gunOfItem(int item)
    {
        if (item < 0 || item >= kItemCount)
            return nullptr;
        for (const CsGun &g : kGuns)
        {
            if (std::strcmp(g.item, kItems[item].name) == 0)
                return &g;
        }
        return nullptr;
    }

    /// The items of menu `menu`, in its order: their indices into kItems.
    /// Returns how many (at most `room`).
    /// What `team` is offered on page `menu`, as 1.6's buy menu lists it
    /// for that team: its own only, in its order.
    inline constexpr const char *kBuyOrderT[] = {
        "GLOCK 18", "USP", "P228", "DESERT EAGLE", "DUAL ELITES", "M3", "XM1014", "MAC-10", "MP5", "UMP45", "P90",
        "GALIL", "AK-47", "SCOUT", "SG 552", "AWP", "G3SG1", "M249", "KEVLAR", "KEVLAR+HELMET", "FLASHBANG",
        "HE GRENADE", "SMOKE GRENADE", "NIGHTVISION"};
    inline constexpr const char *kBuyOrderCT[] = {
        "GLOCK 18", "USP", "P228", "DESERT EAGLE", "FIVE-SEVEN", "M3", "XM1014", "TMP", "MP5", "UMP45", "P90",
        "FAMAS", "SCOUT", "M4A1", "AUG", "SG 550", "AWP", "M249", "KEVLAR", "KEVLAR+HELMET", "FLASHBANG",
        "HE GRENADE", "SMOKE GRENADE", "DEFUSE KIT", "NIGHTVISION", "SHIELD"};
    inline int itemsOfMenu(int menu, int *out, int room, std::uint8_t team)
    {
        int n = 0;
        const auto add = [&](const char *const *order, int count)
        {
            for (int k = 0; k < count && n < room; ++k)
                for (int i = 0; i < kItemCount; ++i)
                    if (kItems[i].menu == menu && std::strcmp(kItems[i].name, order[k]) == 0)
                        out[n++] = i;
        };
        if (team == kTeamT)
            add(kBuyOrderT, static_cast<int>(sizeof(kBuyOrderT) / sizeof(kBuyOrderT[0])));
        else
            add(kBuyOrderCT, static_cast<int>(sizeof(kBuyOrderCT) / sizeof(kBuyOrderCT[0])));
        return n;
    }

    // ------------------------------------------------------------- what is held

    /// Counter-Strike's own view models (models/v_*.mdl), and the clips of
    /// each that stand for what Half-Life's weapon code asks its own to play
    /// (HlMonsters.hpp: "idle1", "draw", "fire", "reload"...). The USP and the
    /// M4A1 are held as the game starts them: without their silencers. The
    /// pump guns load a shell at a time, as Half-Life's shotgun does
    /// (start_reload, then "insert" for its "reload", "after_reload" for its
    /// "pump").
    struct CsView
    {
        const char *model;
        const char *idle;
        const char *draw;
        const char *shoot;
        const char *shootLast; ///< the last round (nullptr: as any shot)
        const char *reload;
        const char *reloadEnd; ///< a pump gun's after its shells, else nullptr
        const char *pin;       ///< a grenade's pin pulled, else nullptr
    };
    inline constexpr CsView kViews[] = {
        {"v_knife", "idle", "draw", "slash1", nullptr, nullptr, nullptr, nullptr},
        {"v_glock18", "idle1", "draw", "shoot", "shoot_empty", "reload", nullptr, nullptr},
        {"v_usp", "idle_unsil", "draw_unsil", "shoot1_unsil", "shootlast_unsil", "reload_unsil", nullptr, nullptr},
        {"v_p228", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"v_deagle", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"v_fiveseven", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"v_elite", "idle", "draw", "shoot_right1", "shoot_rightlast", "reload", nullptr, nullptr},
        {"v_m3", "idle", "draw", "shoot1", nullptr, "insert", "after_reload", nullptr},
        {"v_xm1014", "idle", "draw", "shoot1", nullptr, "insert", "after_reload", nullptr},
        {"v_mac10", "idle1", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_tmp", "idle1", "draw", "shoot", nullptr, "reload", nullptr, nullptr},
        {"v_mp5", "idle1", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_ump45", "idle1", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_p90", "idle", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_galil", "idle", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_famas", "idle", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_ak47", "idle1", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_m4a1", "idle_unsil", "draw_unsil", "shoot1_unsil", nullptr, "reload_unsil", nullptr, nullptr},
        {"v_scout", "idle1", "draw", "shoot", nullptr, "reload", nullptr, nullptr},
        {"v_sg552", "idle", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_aug", "idle", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_awp", "idle1", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_g3sg1", "idle1", "draw", "shoot", nullptr, "reload", nullptr, nullptr},
        {"v_sg550", "idle1", "draw", "shoot", nullptr, "reload", nullptr, nullptr},
        {"v_m249", "idle1", "draw", "shoot1", nullptr, "reload", nullptr, nullptr},
        {"v_hegrenade", "idle", "deploy", "throw", nullptr, nullptr, nullptr, "pullpin"},
        {"v_flashbang", "idle", "deploy", "throw", nullptr, nullptr, nullptr, "pullpin"},
        {"v_smokegrenade", "idle", "deploy", "throw", nullptr, nullptr, nullptr, "pullpin"},
        {"v_c4", "idle1", "draw", "pressbutton", nullptr, nullptr, nullptr, nullptr},
        // With the tactical shield (models/shield/): the knife, the pistols
        // and the grenades, each also with the shield's own three clips
        // (shield_up, shield_down, shield_idle).
        {"shield/v_shield_knife", "idle", "deploy", "slash", nullptr, nullptr, nullptr, nullptr},
        {"shield/v_shield_glock18", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"shield/v_shield_usp", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"shield/v_shield_p228", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"shield/v_shield_deagle", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"shield/v_shield_fiveseven", "idle1", "draw", "shoot1", "shoot_empty", "reload", nullptr, nullptr},
        {"shield/v_shield_hegrenade", "idle1", "draw", "throw", nullptr, nullptr, nullptr, "pullpin"},
        {"shield/v_shield_flashbang", "idle1", "draw", "throw", nullptr, nullptr, nullptr, "pullpin"},
        {"shield/v_shield_smokegrenade", "idle1", "draw", "throw", nullptr, nullptr, nullptr, "pullpin"},
    };
    inline constexpr int kViewCount = static_cast<int>(sizeof(kViews) / sizeof(kViews[0]));
    /// Those a map has in it, not left to the common archive to be read when
    /// first taken up: what every player starts with, and the bomb.
    inline bool viewAlwaysInMap(const char *model)
    {
        return std::strcmp(model, "v_knife") == 0 || std::strcmp(model, "v_glock18") == 0 ||
               std::strcmp(model, "v_usp") == 0 || std::strcmp(model, "v_c4") == 0;
    }

    /// What a console sent the game in DS mode holds (its 4 MB has room
    /// for a few of the guns' own models, not all): these, sent with the map
    /// (the converter's guest stream, packed), and each other gun as the one
    /// of these of its kind -- a Galil as the AK-47, a TMP as the MP5. Bought
    /// as itself, fired as itself (its numbers, its sound); only what is in
    /// hand is another's. (In DSi mode it asks the host for the gun's own.)
    inline constexpr const char *kGuestViews[] = {"v_deagle", "v_m3",   "v_mp5",       "v_ak47",
                                                  "v_m4a1",   "v_awp", "v_hegrenade", "v_flashbang"};
    inline constexpr struct
    {
        const char *view, *as;
    } kGuestStandIns[] = {
        {"v_p228", "v_deagle"}, {"v_fiveseven", "v_deagle"}, {"v_elite", "v_deagle"}, {"v_xm1014", "v_m3"},
        {"v_mac10", "v_mp5"},   {"v_tmp", "v_mp5"},           {"v_ump45", "v_mp5"},   {"v_p90", "v_mp5"},
        {"v_galil", "v_ak47"},  {"v_sg552", "v_ak47"},        {"v_m249", "v_ak47"},   {"v_famas", "v_m4a1"},
        {"v_aug", "v_m4a1"},    {"v_scout", "v_awp"},         {"v_g3sg1", "v_awp"},   {"v_sg550", "v_awp"},
        {"v_smokegrenade", "v_flashbang"},
    };
    /// Whether this console holds the guests' few (set as a map is begun:
    /// sent the game, in DS mode).
    constinit inline bool guestViews = false;
    inline const char *guestView(const char *model)
    {
        if (!guestViews || model == nullptr)
            return model;
        for (const auto &one : kGuestStandIns)
            if (std::strcmp(one.view, model) == 0)
                return one.as;
        return model;
    }

    inline const CsView *viewNamed(const char *model)
    {
        for (const CsView &v : kViews)
        {
            if (model != nullptr && std::strcmp(v.model, model) == 0)
                return &v;
        }
        return nullptr;
    }

    /// The clips of a view model the game may play (for the converter).
    inline int viewClips(const CsView &v, const char **out, int room)
    {
        int n = 0;
        const bool knife = std::strcmp(v.model, "v_knife") == 0;
        const bool shield = std::strncmp(v.model, "shield/", 7) == 0;
        // (The M4A1 and the USP: their silenced selves' too, and putting the
        // silencer on and off.)
        const bool m4 = std::strcmp(v.model, "v_m4a1") == 0, usp = std::strcmp(v.model, "v_usp") == 0;
        const bool sil = m4 || usp;
        const char *const all[] = {v.idle, v.draw, v.shoot, v.shootLast, v.reload, v.reloadEnd, v.pin,
                                   v.reloadEnd != nullptr ? "start_reload" : nullptr, knife ? "slash2" : nullptr,
                                   knife ? "midslash1" : nullptr, shield ? "shield_up" : nullptr,
                                   shield ? "shield_down" : nullptr, shield ? "shield_idle" : nullptr,
                                   sil ? "idle" : nullptr, sil ? "draw" : nullptr, sil ? "shoot1" : nullptr,
                                   sil ? "reload" : nullptr, usp ? "shootlast" : nullptr,
                                   sil ? "add_silencer" : nullptr, sil ? "detach_silencer" : nullptr};
        for (const char *c : all)
        {
            if (c == nullptr || n >= room)
                continue;
            bool have = false;
            for (int k = 0; k < n; ++k)
                have = have || std::strcmp(out[k], c) == 0;
            if (!have)
                out[n++] = c;
        }
        return n;
    }

    inline bool startsWith(const char *text, const char *prefix)
    {
        return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
    }

    /// What Half-Life's weapon code asks to be played (`clip`), as a
    /// Counter-Strike view model has it. Its own name if it has none for it.
    inline const char *viewClip(const CsView &v, const char *clip)
    {
        if (clip == nullptr)
            return clip;
        // (The knife: the crowbar's three swings as its slashes -- right,
        // left, and the middle one.)
        if (std::strcmp(v.model, "v_knife") == 0 && std::strncmp(clip, "attack", 6) == 0)
            return clip[6] == '1' ? "slash1" : (clip[6] == '2' ? "slash2" : "midslash1");
        if (std::strcmp(v.model, "shield/v_shield_knife") == 0 && std::strncmp(clip, "attack", 6) == 0)
            return "slash";
        if (std::strcmp(clip, "start_reload") == 0)
            return v.reloadEnd != nullptr ? "start_reload" : (v.reload != nullptr ? v.reload : clip);
        if (std::strcmp(clip, "pump") == 0)
            return v.reloadEnd != nullptr ? v.reloadEnd : v.idle;
        if (std::strcmp(clip, "pinpull") == 0)
            return v.pin != nullptr ? v.pin : clip;
        if (std::strcmp(clip, "draw") == 0 || std::strcmp(clip, "draw1") == 0 || std::strcmp(clip, "deploy") == 0 ||
            std::strcmp(clip, "up") == 0)
            return v.draw;
        if (startsWith(clip, "reload"))
            return v.reload != nullptr ? v.reload : v.idle;
        if (std::strcmp(clip, "shoot_empty") == 0)
            return v.shootLast != nullptr ? v.shootLast : v.shoot;
        if (startsWith(clip, "shoot") || startsWith(clip, "fire") || startsWith(clip, "attack") ||
            startsWith(clip, "throw") || std::strcmp(clip, "grenade") == 0)
            return v.shoot;
        if (startsWith(clip, "idle") || startsWith(clip, "longidle") || startsWith(clip, "sm_idle") ||
            startsWith(clip, "fidget") || startsWith(clip, "deepidle"))
            return v.idle;
        return clip;
    }

    /// The tactical shield's view model of `view` (the knife's, a pistol's,
    /// a grenade's), or nullptr: none.
    inline const char *shieldView(const char *view)
    {
        static constexpr struct
        {
            const char *view, *shield;
        } kShieldViews[] = {
            {"v_knife", "shield/v_shield_knife"},           {"v_glock18", "shield/v_shield_glock18"},
            {"v_usp", "shield/v_shield_usp"},               {"v_p228", "shield/v_shield_p228"},
            {"v_deagle", "shield/v_shield_deagle"},         {"v_fiveseven", "shield/v_shield_fiveseven"},
            {"v_hegrenade", "shield/v_shield_hegrenade"},   {"v_flashbang", "shield/v_shield_flashbang"},
            {"v_smokegrenade", "shield/v_shield_smokegrenade"},
        };
        for (const auto &one : kShieldViews)
            if (view != nullptr && std::strcmp(one.view, view) == 0)
                return one.shield;
        return nullptr;
    }
    inline bool isShieldView(const char *view) { return view != nullptr && std::strncmp(view, "shield/", 7) == 0; }

    // ------------------------------------------------------------- the player

    /// The player's part of the game that Half-Life has no place for.
    struct PlayerCs
    {
        int money = 800;
        std::uint8_t team = kTeamCT;
        std::uint8_t faction = 0;
        int primary = kNoItem; ///< what they bought for each slot (kItems), for its name
        int pistol = kNoItem;
        /// The grenades carried (the stand-in's count is them all together),
        /// and which is in hand (a Gear: kGearHe, kGearFlash, kGearSmoke):
        /// the one bought last, until there are none of it left
        /// -- or the next kind, the grenades taken again (nextGrenade).
        int he = 0, flashes = 0, smokes = 0;
        std::uint8_t hand = kGearHe;
        bool helmet = false;
        bool defuser = false;
        /// The night vision goggles: carried (until death), and on.
        bool nvg = false, nvgOn = false;
        /// The tactical shield: carried (in place of a primary, until death),
        /// and raised (the second trigger: nothing fired, more of the body
        /// behind it, slower).
        bool shield = false, shieldUp = false;
        /// The M4A1's and the USP's silencers: on (each bought without).
        bool m4Silenced = false, uspSilenced = false;
        /// Each slot's spare rounds (kSlotPrimary, kSlotPistol), and which
        /// of the two the stand-ins' one shared pool holds now when both
        /// draw on it (a 9mm pistol's and a rifle's: ammoSync).
        int spare[2] = {0, 0};
        std::uint8_t poolSlot = kSlotPistol;
        bool inBuyZone = false; ///< as the last frame found
    };
    inline PlayerCs player;

    /// The goggles switched (slot 5: Input.cpp) or bought, for the game to
    /// say: 1 on, 2 off, 3 bought, 0 nothing new.
    inline int nvgSaid = 0;
    inline void nvgToggle(PlayerCs &cs)
    {
        if (!cs.nvg)
            return;
        cs.nvgOn = !cs.nvgOn;
        nvgSaid = cs.nvgOn ? 1 : 2;
    }

    /// What was bought that the stand-in `weapon` is (kNoItem: nothing).
    inline int itemInHand(int weapon, const PlayerCs &cs)
    {
        int item = kNoItem;
        if (weapon == kWeaponGlock || weapon == kWeaponPython)
            item = cs.pistol;
        else if (weapon == kWeaponMp5 || weapon == kWeaponShotgun || weapon == kWeaponCrossbow)
            item = cs.primary;
        return item >= 0 && item < kItemCount && kItems[item].standIn == weapon ? item : kNoItem;
    }
    /// The silencer of what is in hand, if it is the M4A1 or the USP (not
    /// with the shield): nullptr, none.
    inline bool *silencerOf(int weapon, PlayerCs &cs)
    {
        const int item = itemInHand(weapon, cs);
        if (item == kNoItem)
            return nullptr;
        if (std::strcmp(kItems[item].name, "M4A1") == 0)
            return &cs.m4Silenced;
        if (std::strcmp(kItems[item].name, "USP") == 0 && !cs.shield)
            return &cs.uspSilenced;
        return nullptr;
    }
    inline bool silencedInHand(int weapon, const PlayerCs &cs)
    {
        const bool *on = silencerOf(weapon, const_cast<PlayerCs &>(cs));
        return on != nullptr && *on;
    }
    /// The silencer being put on or taken off: how long yet (nothing fired
    /// meanwhile: the M4A1's two seconds, the USP's three), and what was
    /// done, for the game to show and say (1 on, 2 off).
    inline int silencerMs = 0;
    inline int silencerSaid = 0;
    /// A clip of the M4A1's or the USP's, as its model has it with the
    /// silencer on, if it is (their own clips without "_unsil").
    inline const char *heldClip(const char *model, const char *clip, const PlayerCs &cs)
    {
        if (model == nullptr || clip == nullptr)
            return clip;
        const bool on = (std::strcmp(model, "v_m4a1") == 0 && cs.m4Silenced) ||
                        (std::strcmp(model, "v_usp") == 0 && cs.uspSilenced);
        if (!on)
            return clip;
        static constexpr struct
        {
            const char *off, *on;
        } kSilenced[] = {{"idle_unsil", "idle"},     {"draw_unsil", "draw"},           {"shoot1_unsil", "shoot1"},
                         {"reload_unsil", "reload"}, {"shootlast_unsil", "shootlast"}};
        for (const auto &one : kSilenced)
            if (std::strcmp(clip, one.off) == 0)
                return one.on;
        return clip;
    }

    /// How fast the player runs with `weapon` (a stand-in) in hand -- what
    /// was bought for it -- or the bomb: the game's GetMaxSpeed. The shield
    /// raised, 180, whatever is held with it.
    inline int speedFor(int weapon, const PlayerCs &cs, bool bombInHand, bool zoomed = false)
    {
        if (bombInHand)
            return 250;
        if (cs.shield && cs.shieldUp)
            return 180;
        const CsGun *g = nullptr;
        if (weapon == kWeaponShotgun || weapon == kWeaponMp5 || weapon == kWeaponCrossbow)
            g = gunOfItem(cs.primary);
        else if (weapon == kWeaponGlock || weapon == kWeaponPython)
            g = gunOfItem(cs.pistol);
        if (g != nullptr && zoomed && weapon == kWeaponCrossbow && g->zoomSpeed > 0)
            return g->zoomSpeed;
        return g != nullptr ? g->speed : 250;
    }

    /// What is left of `damage` from a round of `g` that went `distance`
    /// units: the game's damage * RANGE_MODIFER ^ (distance / 500) -- each
    /// whole 500 a step, what is past the last one between two steps.
    inline int damageAfter(const CsGun &g, int damage, int distance)
    {
        if (g.rangeMod >= 1000 || distance <= 0)
            return damage;
        std::uint32_t left = 65536; // (Q16)
        for (int d = distance; d >= 500; d -= 500)
            left = left * static_cast<std::uint32_t>(g.rangeMod) / 1000;
        const std::uint32_t part = static_cast<std::uint32_t>(distance % 500) * 131; // (of 500, Q16)
        const std::uint32_t step = left - left * static_cast<std::uint32_t>(g.rangeMod) / 1000;
        left -= static_cast<std::uint32_t>((static_cast<std::uint64_t>(step) * part) >> 16);
        const int after = static_cast<int>((static_cast<std::uint64_t>(damage) * left + 32768) >> 16);
        return after > 0 ? after : 1;
    }

    /// Whether the player's tactical shield takes a blow from `from` that
    /// lands at hit group `group` (csShieldHook): from in front of them --
    /// the shield at their side, the body behind it, not the head; raised,
    /// a wider way round and the head too. (The game asks whether the round
    /// met the shield's own box; this is that box's reach, by the way the
    /// blow comes.)
    inline int shieldStruck = 0; ///< blows taken by it since the game last said so
    inline bool shieldShown = false; ///< the view model's shield is up (its clip played)
    inline bool shieldBlocks(const PlayerState &p, const fx *from, std::uint8_t group)
    {
        if (!player.shield || from == nullptr || p.health <= 0)
            return false;
        const fx flat[3] = {from[0] - p.origin[0], from[1] - p.origin[1], 0};
        const fx far = fxLength(flat);
        if (far <= 0)
            return false;
        // (The cosine of the way it comes from the way they face, Q12.)
        const std::int64_t along = static_cast<std::int64_t>(flat[0]) * p.facingXY[0] +
                                   static_cast<std::int64_t>(flat[1]) * p.facingXY[1];
        const std::int64_t cosine = along / far;
        if (cosine < (player.shieldUp ? 2048 : 3355)) // (60 degrees either way raised, 35 lowered)
            return false;
        if (!player.shieldUp && group == 1) // (HlModel.hpp's kHitHead)
            return false;
        ++shieldStruck;
        return true;
    }

    // ---------------------------------------------------- smoke and fire
    //
    // What the grenades leave (HlCsBots.hpp makes them and burns with them;
    // EffectDraw draws them).

    /// A smoke grenade's cloud: rising to its size, standing, thinning
    /// away. What is behind it is not seen through it -- by a bot either
    /// (csSightHook). Counter-Strike's lasts some 18 seconds.
    struct SmokeCloud
    {
        fx at[3] = {0, 0, 0};
        int ms = 0; ///< since it went off; 0: none
    };
    inline constexpr int kSmokeClouds = 4;
    inline constexpr int kSmokeGrowMs = 1500, kSmokeWholeMs = 15000, kSmokeGoneMs = 18000;
    inline constexpr int kSmokeRadius = 150; ///< at its size
    constinit inline SmokeCloud smokeClouds[kSmokeClouds];

    /// How big a cloud is now (units), and how thick (of 31): 0, not there.
    inline int smokeRadius(const SmokeCloud &cloud)
    {
        if (cloud.ms <= 0 || cloud.ms >= kSmokeGoneMs)
            return 0;
        return cloud.ms < kSmokeGrowMs ? 40 + (kSmokeRadius - 40) * cloud.ms / kSmokeGrowMs : kSmokeRadius;
    }
    inline int smokeThickness(const SmokeCloud &cloud)
    {
        if (cloud.ms <= 0 || cloud.ms >= kSmokeGoneMs)
            return 0;
        if (cloud.ms < 400)
            return 31 * cloud.ms / 400;
        if (cloud.ms > kSmokeWholeMs)
            return 31 * (kSmokeGoneMs - cloud.ms) / (kSmokeGoneMs - kSmokeWholeMs);
        return 31;
    }

    /// Where puff `n` (of kSmokePuffs) of a cloud is drawn, and how big
    /// (units either way): one in the middle, the rest round it.
    inline constexpr int kSmokePuffs = 7;
    inline constexpr std::int32_t kRingCos[6] = {4096, 2048, -2048, -4096, -2048, 2048};
    inline constexpr std::int32_t kRingSin[6] = {0, 3547, 3547, 0, -3547, -3547};
    inline int smokePuff(const SmokeCloud &cloud, int n, fx *at)
    {
        const int radius = smokeRadius(cloud);
        at[0] = cloud.at[0];
        at[1] = cloud.at[1];
        at[2] = cloud.at[2] + fxFromInt(radius / 3 + 16);
        if (n == 0)
            return radius * 3 / 4;
        const int out = radius * 11 / 20;
        at[0] += static_cast<fx>(out * kRingCos[(n - 1) % 6]); // (Q12 times whole units)
        at[1] += static_cast<fx>(out * kRingSin[(n - 1) % 6]);
        at[2] += fxFromInt((n & 1) ? radius / 6 : -radius / 8);
        return radius * 3 / 5;
    }
    /// The touch screen's pictures of Counter-Strike's weapons
    /// (ui/csweapons.hic): the converter takes each from the game's own
    /// sprites/weapon_<name>.txt -- its 640 "weapon" picture -- in this
    /// order: the knife, the guns as kGuns has them, the grenades, the bomb.
    inline constexpr const char *kCsIconsFile = "ui/csweapons.hic";
    inline constexpr const char *kCsIconNames[] = {
        "knife", "glock18", "usp",  "p228", "deagle", "fiveseven", "elite", "m3",    "xm1014", "mac10",
        "tmp",   "mp5navy", "ump45", "p90", "galil",  "famas",     "ak47",  "m4a1",  "scout",  "sg552",
        "aug",   "awp",     "g3sg1", "sg550", "m249", "hegrenade", "flashbang", "c4", "smokegrenade"};
    inline constexpr int kCsIcons = static_cast<int>(sizeof(kCsIconNames) / sizeof(kCsIconNames[0]));
    inline constexpr int kCsIconKnife = 0, kCsIconHe = 25, kCsIconFlash = 26, kCsIconC4 = 27, kCsIconSmoke = 28;

    // The grenades, by kind.
    inline constexpr std::uint8_t kGrenadeKinds[] = {kGearHe, kGearFlash, kGearSmoke};
    inline constexpr int kGrenadeKindCount = static_cast<int>(sizeof(kGrenadeKinds) / sizeof(kGrenadeKinds[0]));
    inline bool isGrenade(std::uint8_t gear)
    {
        return gear == kGearHe || gear == kGearFlash || gear == kGearSmoke;
    }
    /// How many of kind `gear` (a grenade's: isGrenade) are carried.
    inline int &grenadesOf(PlayerCs &cs, std::uint8_t gear)
    {
        return gear == kGearFlash ? cs.flashes : gear == kGearSmoke ? cs.smokes : cs.he;
    }
    inline int grenadesOf(const PlayerCs &cs, std::uint8_t gear)
    {
        return gear == kGearHe ? cs.he : gear == kGearFlash ? cs.flashes : gear == kGearSmoke ? cs.smokes : 0;
    }
    inline int grenadesCarried(const PlayerCs &cs) { return cs.he + cs.flashes + cs.smokes; }
    /// The kind in hand: the one chosen while there is one of it, else
    /// the first kind there is (kGearNone: none at all).
    inline std::uint8_t grenadeInHand(const PlayerCs &cs)
    {
        if (isGrenade(cs.hand) && grenadesOf(cs, cs.hand) > 0)
            return cs.hand;
        for (const std::uint8_t kind : kGrenadeKinds)
            if (grenadesOf(cs, kind) > 0)
                return kind;
        return kGearNone;
    }
    /// The grenades taken again in hand: the next kind carried. Whether
    /// it is another.
    inline bool nextGrenade(PlayerCs &cs)
    {
        const std::uint8_t now = grenadeInHand(cs);
        int at = 0;
        while (at < kGrenadeKindCount && kGrenadeKinds[at] != now)
            ++at;
        for (int step = 1; step <= kGrenadeKindCount; ++step)
        {
            const std::uint8_t kind = kGrenadeKinds[(at + step) % kGrenadeKindCount];
            if (grenadesOf(cs, kind) > 0)
            {
                cs.hand = kind;
                return kind != now;
            }
        }
        return false;
    }
    /// After one of kind `thrown` has gone: the hand's choice kept while
    /// there are more of it, else the next kind there is.
    inline void grenadeGone(PlayerCs &cs, std::uint8_t thrown)
    {
        if (grenadesOf(cs, thrown) > 0)
            return;
        int at = 0;
        while (at < kGrenadeKindCount && kGrenadeKinds[at] != thrown)
            ++at;
        for (int step = 1; step < kGrenadeKindCount; ++step)
        {
            const std::uint8_t kind = kGrenadeKinds[(at + step) % kGrenadeKindCount];
            if (grenadesOf(cs, kind) > 0)
            {
                cs.hand = kind;
                return;
            }
        }
    }
    static_assert(kCsIconHe == 1 + static_cast<int>(sizeof(kGuns) / sizeof(kGuns[0])));
    /// The picture for Half-Life weapon `weapon` (its stand-in) as what was
    /// bought for it; -1: none.
    inline int iconFor(int weapon, const PlayerCs &cs)
    {
        auto gunIcon = [](int item) -> int
        {
            const CsGun *g = gunOfItem(item);
            return g != nullptr ? 1 + static_cast<int>(g - kGuns) : -1;
        };
        switch (weapon)
        {
        case kWeaponCrowbar: return kCsIconKnife;
        case kWeaponGlock:
        case kWeaponPython: return gunIcon(cs.pistol);
        case kWeaponMp5:
        case kWeaponShotgun:
        case kWeaponCrossbow: return gunIcon(cs.primary);
        case kWeaponGrenade:
            switch (grenadeInHand(cs))
            {
            case kGearFlash: return kCsIconFlash;
            case kGearSmoke: return kCsIconSmoke;
            default: return kCsIconHe; // (the HE; the fire too, which has no picture of its own)
            }
        default: return -1;
        }
    }

    /// Why something cannot be bought.
    enum class Buy : std::uint8_t
    {
        Ok,
        NotHere,     ///< not in a buy zone
        OtherTeam,   ///< the other team's
        NoMoney,
        Have,        ///< already carried, or carried as much as may be
    };

    inline Buy canBuy(const PlayerState &state, const PlayerCs &cs, int item)
    {
        if (item < 0 || item >= kItemCount)
            return Buy::Have;
        const Item &it = kItems[item];
        if (!cs.inBuyZone)
            return Buy::NotHere;
        if ((it.teams & (1u << cs.team)) == 0)
            return Buy::OtherTeam;
        if ((it.slot == kSlotPrimary && cs.primary == item) || (it.slot == kSlotPistol && cs.pistol == item))
            return Buy::Have;
        switch (it.gear)
        {
        case kGearKevlar:
            if (state.inv.armor >= 100)
                return Buy::Have;
            break;
        case kGearKevlarHelmet:
            if (state.inv.armor >= 100 && cs.helmet)
                return Buy::Have;
            break;
        case kGearHe:
            if (cs.he >= 1)
                return Buy::Have;
            break;
        case kGearFlash:
            if (cs.flashes >= 2)
                return Buy::Have;
            break;
        case kGearSmoke:
            if (cs.smokes >= 1)
                return Buy::Have;
            break;
        case kGearDefuser:
            if (cs.defuser)
                return Buy::Have;
            break;
        case kGearNvg:
            if (cs.nvg)
                return Buy::Have;
            break;
        case kGearShield:
            if (cs.shield)
                return Buy::Have;
            break;
        default:
            break;
        }
        // (Kevlar and a helmet with the kevlar already worn: the helmet's
        // part of the price, as the game charges it.)
        const int price = it.gear == kGearKevlarHelmet && state.inv.armor >= 100 ? 350 : it.price;
        if (cs.money < price)
            return Buy::NoMoney;
        return Buy::Ok;
    }

    /// The weapons each slot's stand-ins are: buying for a slot drops what
    /// the slot had (Counter-Strike carries one of each).
    inline void dropSlot(PlayerState &state, int slot)
    {
        static const std::uint8_t kPrimary[] = {kWeaponShotgun, kWeaponMp5, kWeaponCrossbow};
        static const std::uint8_t kPistol[] = {kWeaponGlock, kWeaponPython};
        if (slot == kSlotPrimary)
        {
            for (std::uint8_t w : kPrimary)
                takeWeapon(state, w);
        }
        else if (slot == kSlotPistol)
        {
            for (std::uint8_t w : kPistol)
                takeWeapon(state, w);
        }
    }

    /// Full of what a stand-in fires: a clip in it and as much again as the
    /// game lets be carried besides.
    inline void fillStandIn(PlayerState &state, int weapon)
    {
        Inventory &inv = state.inv;
        switch (weapon)
        {
        case kWeaponGlock:
            inv.clipGlock = 17;
            inv.ammo9mm = inv.ammo9mm < 120 ? 120 : inv.ammo9mm;
            break;
        case kWeaponPython:
            inv.clipPython = 6;
            inv.ammo357 = 36;
            break;
        case kWeaponShotgun:
            inv.clipShotgun = 8;
            inv.ammoBuck = 32;
            break;
        case kWeaponMp5:
            inv.clipMp5 = 50;
            inv.ammo9mm = 250;
            inv.ammoAr = 0; // (Counter-Strike's have no grenade launcher)
            break;
        case kWeaponCrossbow:
            if (state.arsenal != nullptr)
            {
                state.arsenal->boltsClip = 5;
                state.arsenal->bolts = 50;
            }
            break;
        case kWeaponGrenade:
            inv.grenades = 1;
            break;
        default:
            break;
        }
    }

    /// Buy item `item`: paid for, given (its stand-in, full), and the
    /// weapon to take in hand returned -- kWeaponNone for what is worn.
    /// Nothing changes unless canBuy says Ok.
    // ---------------------------------------------------------- the rounds
    //
    // Each slot's spare rounds are the player's own (PlayerCs::spare), as
    // 1.6 keeps one count of each kind. The stand-ins keep theirs where
    // Half-Life does, and two of them -- the 9mm pistol's and the MP5's,
    // which most pistols and most primaries stand in for -- share one: it
    // holds the spare rounds of the one in hand, and is handed over as the
    // other is taken (ammoSync).

    /// Where the stand-in for slot `slot` keeps its spare rounds (nullptr:
    /// nothing in the slot).
    inline std::int16_t *poolOf(PlayerState &state, const PlayerCs &cs, int slot)
    {
        const int item = slot == kSlotPrimary ? cs.primary : (slot == kSlotPistol ? cs.pistol : kNoItem);
        if (item < 0 || item >= kItemCount)
            return nullptr;
        switch (kItems[item].standIn)
        {
        case kWeaponGlock:
        case kWeaponMp5: return &state.inv.ammo9mm;
        case kWeaponPython: return &state.inv.ammo357;
        case kWeaponShotgun: return &state.inv.ammoBuck;
        case kWeaponCrossbow: return state.arsenal != nullptr ? &state.arsenal->bolts : nullptr;
        default: return nullptr;
        }
    }
    /// The pool the hand draws from, made the one of the slot in hand, and
    /// each slot's count kept from it: every frame.
    inline void ammoSync(PlayerState &state, PlayerCs &cs)
    {
        std::int16_t *primary = poolOf(state, cs, kSlotPrimary), *pistol = poolOf(state, cs, kSlotPistol);
        const int current = state.inv.current;
        const int item = current == kWeaponGlock || current == kWeaponPython ? cs.pistol
                         : current == kWeaponMp5 || current == kWeaponShotgun || current == kWeaponCrossbow ? cs.primary
                                                                                                             : kNoItem;
        const int slot = item == kNoItem ? -1 : (item == cs.pistol ? kSlotPistol : kSlotPrimary);
        if (primary != nullptr && primary == pistol)
        {
            if (slot < 0)
                return;
            if (cs.poolSlot != slot)
            {
                cs.spare[cs.poolSlot] = *primary;
                *primary = static_cast<std::int16_t>(cs.spare[slot]);
                cs.poolSlot = static_cast<std::uint8_t>(slot);
            }
            cs.spare[slot] = *primary;
            return;
        }
        if (primary != nullptr)
            cs.spare[kSlotPrimary] = *primary;
        if (pistol != nullptr)
            cs.spare[kSlotPistol] = *pistol;
    }
    /// Slot `slot`'s spare rounds made `count` (each slot's counted as it
    /// was: ammoSync before anything was done to the pool).
    inline void setSpare(PlayerState &state, PlayerCs &cs, int slot, int count)
    {
        if (slot != kSlotPrimary && slot != kSlotPistol)
            return;
        std::int16_t *pool = poolOf(state, cs, slot);
        const std::int16_t *other = poolOf(state, cs, slot == kSlotPrimary ? kSlotPistol : kSlotPrimary);
        if (pool != nullptr && pool == other)
            cs.poolSlot = static_cast<std::uint8_t>(slot);
        cs.spare[slot] = count;
        if (pool != nullptr)
            *pool = static_cast<std::int16_t>(count);
    }
    /// PRIMARY AMMO (`slot` kSlotPrimary) or SECONDARY AMMO: boxes for the
    /// gun in the slot -- the pistol's, with the shield -- until it carries
    /// all it may or the money is out, as 1.6's menu buys them.
    inline Buy buyAmmo(PlayerState &state, PlayerCs &cs, int slot)
    {
        if (!cs.inBuyZone)
            return Buy::NotHere;
        ammoSync(state, cs);
        if (slot == kSlotPrimary && cs.primary == kNoItem && cs.shield)
            slot = kSlotPistol;
        const CsGun *g = gunOfItem(slot == kSlotPrimary ? cs.primary : cs.pistol);
        if (g == nullptr || cs.spare[slot] >= g->reserve)
            return Buy::Have;
        const auto &box = kAmmoBoxes[g->ammo];
        if (cs.money < box.price)
            return Buy::NoMoney;
        int count = cs.spare[slot];
        while (count < g->reserve && cs.money >= box.price)
        {
            count = count + box.amount > g->reserve ? g->reserve : count + box.amount;
            cs.money -= box.price;
        }
        setSpare(state, cs, slot, count);
        return Buy::Ok;
    }

    inline void giveItem(PlayerState &state, PlayerCs &cs, int item, std::uint8_t &takeInHand, bool pickedUp = false);
    inline Buy buy(PlayerState &state, PlayerCs &cs, int item, std::uint8_t &takeInHand)
    {
        takeInHand = kWeaponNone;
        const Buy why = canBuy(state, cs, item);
        if (why != Buy::Ok)
            return why;
        const Item &it = kItems[item];
        const int price = it.gear == kGearKevlarHelmet && state.inv.armor >= 100 ? 350 : it.price;
        cs.money -= price;
        giveItem(state, cs, item, takeInHand);
        return Buy::Ok;
    }

    /// Item `item` given (bought, or picked up): what it is for the slot it
    /// goes in, loaded as it is bought.
    inline void giveItem(PlayerState &state, PlayerCs &cs, int item, std::uint8_t &takeInHand, bool pickedUp)
    {
        takeInHand = kWeaponNone;
        if (item < 0 || item >= kItemCount)
            return;
        const Item &it = kItems[item];
        switch (it.gear)
        {
        case kGearKevlar:
            state.inv.armor = 100;
            return;
        case kGearKevlarHelmet:
            state.inv.armor = 100;
            cs.helmet = true;
            return;
        case kGearDefuser:
            cs.defuser = true;
            return;
        case kGearNvg:
            cs.nvg = true;
            nvgSaid = 3;
            return;
        case kGearShield:
            // (The primary carried let go of: the shield is held in its
            // place, with a pistol or the knife.)
            dropSlot(state, kSlotPrimary);
            cs.primary = kNoItem;
            cs.shield = true;
            cs.shieldUp = false;
            return;
        default:
            break;
        }
        // (A primary: the shield let go of.)
        if (it.slot == kSlotPrimary)
            cs.shield = cs.shieldUp = false;
        // (Each slot's spare rounds counted before the stand-in is given.)
        if (it.slot == kSlotPrimary || it.slot == kSlotPistol)
            ammoSync(state, cs);
        if (it.slot == kSlotPrimary || it.slot == kSlotPistol)
            dropSlot(state, it.slot);
        if (it.slot == kSlotPrimary)
            cs.primary = item;
        else if (it.slot == kSlotPistol)
            cs.pistol = item;
        // (Bought, or picked up: its silencer off, as the game's are.)
        if (std::strcmp(it.name, "M4A1") == 0)
            cs.m4Silenced = false;
        else if (std::strcmp(it.name, "USP") == 0)
            cs.uspSilenced = false;
        giveWeapon(state, it.standIn);
        fillStandIn(state, it.standIn);
        // (A grenade: one more of its kind -- the HE, the flashbangs, the
        // smoke, the fire -- the stand-in carrying them all, the one bought
        // in hand.)
        if (isGrenade(it.gear))
        {
            grenadesOf(cs, it.gear) += 1;
            cs.hand = it.gear;
            state.inv.grenades = static_cast<std::int16_t>(grenadesCarried(cs));
        }
        // (Loaded as 1.6 gives a gun bought: its magazine, and nothing
        // besides -- the rounds are bought apart, PRIMARY AMMO, SECONDARY
        // AMMO. One picked up comes with what it had: as many as it holds.)
        if (const CsGun *g = gunOfItem(item))
        {
            Inventory &inv = state.inv;
            switch (it.standIn)
            {
            case kWeaponGlock: inv.clipGlock = static_cast<std::int16_t>(g->clip); break;
            case kWeaponMp5: inv.clipMp5 = static_cast<std::int16_t>(g->clip); break;
            case kWeaponPython: inv.clipPython = static_cast<std::int16_t>(g->clip); break;
            case kWeaponShotgun: inv.clipShotgun = static_cast<std::int16_t>(g->clip); break;
            case kWeaponCrossbow:
                if (state.arsenal != nullptr)
                    state.arsenal->boltsClip = static_cast<std::int16_t>(g->clip);
                break;
            default: break;
            }
            setSpare(state, cs, it.slot, pickedUp ? g->reserve : 0);
        }
        takeInHand = it.standIn;
        return;
    }

    // ------------------------------------------------------- guns on the floor
    //
    // What one shot down had in hand lies where they fell, until the next
    // round: a bot's gun, or the player's own rifle. Walked over with nothing
    // in its slot, it is picked up; with USE, it takes the place of the one
    // in that slot, which is let fall in its turn.

    /// Those that can lie about: the bots' guns, as kItems has them, each
    /// with its model on the floor (models/w_*.mdl: the converter cooks
    /// them into a Counter-Strike map).
    inline constexpr const char *kFloorGunItems[] = {"GLOCK 18", "USP", "AK-47", "M4A1"};
    inline constexpr const char *kFloorGunModels[] = {"w_glock18", "w_usp", "w_ak47", "w_m4a1"};
    inline constexpr int kFloorGunKinds = 4;
    inline int itemNamed(const char *name)
    {
        for (int i = 0; i < kItemCount; ++i)
            if (std::strcmp(kItems[i].name, name) == 0)
                return i;
        return kNoItem;
    }
    /// Which of those item `item` is; -1: none (it is not left lying).
    inline int floorKindOf(int item)
    {
        for (int k = 0; k < kFloorGunKinds; ++k)
            if (item >= 0 && itemNamed(kFloorGunItems[k]) == item)
                return k;
        return -1;
    }
    struct FloorGun
    {
        bool active = false;
        fx pos[3] = {0, 0, 0};
        std::uint16_t yaw = 0;
        std::int16_t item = kNoItem;
    };
    inline constexpr int kFloorGuns = 12;
    constinit inline FloorGun floorGuns[kFloorGuns];
    /// Item `item` let fall at `feet` (one that cannot lie about is gone).
    inline void dropGun(const fx *feet, int item)
    {
        if (floorKindOf(item) < 0)
            return;
        static int next = 0;
        int at = -1;
        for (int i = 0; i < kFloorGuns && at < 0; ++i)
            if (!floorGuns[i].active)
                at = i;
        if (at < 0)
            at = next++ % kFloorGuns; // (the oldest, give or take)
        FloorGun &g = floorGuns[at];
        g.active = true;
        for (int k = 0; k < 3; ++k)
            g.pos[k] = feet[k];
        g.yaw = static_cast<std::uint16_t>(feet[0] * 7 + feet[1] * 13);
        g.item = static_cast<std::int16_t>(item);
    }
    inline void clearFloorGuns()
    {
        for (FloorGun &g : floorGuns)
            g = FloorGun();
    }
    /// The player at `feet`: a gun under them taken up -- by itself into an
    /// empty slot, or with `use` in place of the one carried. What was taken
    /// is to be put in hand (`takeInHand`); true if one was.
    inline bool pickUpTick(PlayerState &state, PlayerCs &cs, const fx *feet, bool use, std::uint8_t &takeInHand)
    {
        takeInHand = kWeaponNone;
        int best = -1;
        fx bestFar = fxFromInt(64);
        for (int i = 0; i < kFloorGuns; ++i)
        {
            const FloorGun &g = floorGuns[i];
            if (!g.active)
                continue;
            const fx d[3] = {g.pos[0] - feet[0], g.pos[1] - feet[1], 0};
            const fx up = g.pos[2] - (feet[2] - fxFromInt(36)); // (`feet`: the player's middle)
            const fx far = fxLength(d);
            if (far < bestFar && up < fxFromInt(48) && up > -fxFromInt(48))
            {
                best = i;
                bestFar = far;
            }
        }
        if (best < 0)
            return false;
        FloorGun &g = floorGuns[best];
        const int item = g.item;
        const int slot = kItems[item].slot;
        const int carried = slot == kSlotPrimary ? cs.primary : cs.pistol;
        // (No primary with the shield: as the game has it.)
        if (slot == kSlotPrimary && cs.shield)
            return false;
        if (carried == item && !use)
            return false;
        if (carried != kNoItem && !use)
            return false;
        g.active = false;
        if (carried != kNoItem)
            dropGun(feet, carried);
        if (slot == kSlotPrimary || slot == kSlotPistol)
            dropSlot(state, slot);
        giveItem(state, cs, item, takeInHand, true);
        return true;
    }

    /// What a player starts with: the knife, their team's pistol (the
    /// terrorists' Glock, the counter-terrorists' USP) with two clips, no
    /// armour, and the setup's money.
    inline void startLoadout(PlayerState &state, PlayerCs &cs, const Setup &s)
    {
        cs = PlayerCs();
        cs.team = s.team == kTeamT ? kTeamT : kTeamCT;
        cs.faction = s.faction < kFactions ? s.faction : 0;
        cs.money = startMoney(s);
        state.inv.have = static_cast<std::uint8_t>(state.inv.have | 1u); // (bit 0: the suit, which is the HUD)
        giveWeapon(state, kWeaponCrowbar);
        giveWeapon(state, kWeaponGlock);
        // (The Glock's 20 and two magazines more; the USP's 12 and two more.)
        state.inv.clipGlock = cs.team == kTeamT ? 20 : 12;
        state.inv.ammo9mm = cs.team == kTeamT ? 40 : 24;
        cs.spare[kSlotPistol] = state.inv.ammo9mm;
        cs.poolSlot = kSlotPistol;
        state.inv.armor = 0;
        state.inv.current = kWeaponGlock;
        cs.pistol = cs.team == kTeamT ? 0 : 1; // (kItems: the Glock, the USP)
    }

    /// Where a player of `team` starts: the n-th of their team's starts (n
    /// counted round). False if the map has none for them.
    inline bool teamStart(const LevelView &view, int team, int n, const EntityRecord *&out)
    {
        const auto wanted = static_cast<std::uint16_t>(team == kTeamT ? EntityClass::InfoPlayerDeathmatch
                                                                       : EntityClass::InfoPlayerStart);
        int count = 0;
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
            count += view.entities[e].classId == wanted ? 1 : 0;
        if (count == 0)
            return false;
        int pick = n % count;
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
        {
            if (view.entities[e].classId != wanted)
                continue;
            if (pick-- == 0)
            {
                out = &view.entities[e];
                return true;
            }
        }
        return false;
    }

    /// Whether a player of `team` at `origin` may buy. The map's buy zones
    /// (func_buyzone: LogicKind::BuyZone, whose team is extra[0]); or, on a
    /// map with none, as the game does, round each of their team's starts.
    inline constexpr int kSpawnBuyRadius = 200; ///< units, about a start (a map with no buy zone)
    inline bool inBuyZone(const LevelView &view, int team, const fx origin[3])
    {
        bool zones = false;
        for (std::uint32_t i = 0; i < view.logicCount; ++i)
        {
            const LogicRecord &r = view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::BuyZone))
                continue;
            zones = true;
            if (r.extra[0] != 0 && r.extra[0] != static_cast<std::uint32_t>(team))
                continue;
            bool inside = true;
            for (int a = 0; a < 3 && inside; ++a)
                inside = origin[a] >= fxFromInt(r.mins[a]) && origin[a] <= fxFromInt(r.maxs[a]);
            if (inside)
                return true;
        }
        if (zones)
            return false;
        const auto wanted = static_cast<std::uint16_t>(team == kTeamT ? EntityClass::InfoPlayerDeathmatch
                                                                       : EntityClass::InfoPlayerStart);
        for (std::uint32_t e = 0; e < view.entityCount; ++e)
        {
            const EntityRecord &s = view.entities[e];
            if (s.classId != wanted)
                continue;
            // (Whole units: a sum of squares well inside 32 bits at this radius.)
            const int dx = (origin[0] - s.origin[0]) >> kFxShift, dy = (origin[1] - s.origin[1]) >> kFxShift;
            if (dx > -kSpawnBuyRadius && dx < kSpawnBuyRadius && dy > -kSpawnBuyRadius && dy < kSpawnBuyRadius &&
                dx * dx + dy * dy <= kSpawnBuyRadius * kSpawnBuyRadius)
                return true;
        }
        return false;
    }

    // ------------------------------------------------------------- the buy menu

    // On the touch screen, over the aim pad and the column beside it (the
    // bar above and the slots below stay):
    //
    //   24 +--------------------------------+
    //      |[BACK]  RIFLES          $ 3100  |   the head
    //   40 |[ AK-47   2500 ][ M4A1    3100 ]|
    //      |[ ...          ][ ...          ]|   two columns, five rows
    //  156 +--------------------------------+
    //
    // The first page has the six kinds and CLOSE; a kind's, its items and
    // BACK in the head. An item the player cannot buy is grey (the other
    // team's, or more than they have); touched, it is refused.
    inline constexpr int kBuyTop = kTouchPadTop;
    inline constexpr int kBuyHeadHeight = 16;
    inline constexpr int kBuyRowHeight = 22;
    inline constexpr int kBuyRows = 5;
    inline constexpr int kBuyColumns = 2;
    inline constexpr int kBuyMost = kBuyRows * kBuyColumns;
    inline constexpr int kBuyInset = 2;
    inline constexpr int kBuyBackWidth = 44;

    // ------------------------------------------------------------ the radio
    //
    // The player's orders to the bots on their side (HlCsBots.hpp: the
    // plays they make): AUTO leaves the plays to them. On the BUY button out
    // of a buy zone (RADIO), and on the buy menu's first page -- alive or
    // dead (an order lasts the round).
    //
    // A terrorist's: GO A, GO B (an execute), RUSH, SPLIT, DEFAULT, and the
    // map's ways in. A counter-terrorist's, as their side has it: BOMB A,
    // BOMB B (the bomb is going there: all to it, an anchor kept at the
    // other with enough of them), PUSH (forward, where the terrorists come
    // from), SPREAD (the standard setup again), RETAKE (off the sites, to
    // their start, and in together once it is down), and the map's places
    // to hold. Both: FOLLOW ME, HOLD.
    enum class Order : std::uint8_t
    {
        Auto,
        GoA,      ///< (a counter-terrorist's: BOMB A)
        GoB,      ///< (BOMB B)
        Rush,     ///< (PUSH)
        Split,    ///< (SPREAD)
        Default,  ///< (RETAKE)
        FollowMe,
        Hold,
        // The map's own ways in, by name (HlCsBots.hpp's KnownMap: dust2's
        // A short; nuke's outside to secret, the vent, the ramp) -- or, a
        // counter-terrorist's, its places to hold (dust2's long A, ...).
        Way0,
        Way1,
        Way2,
        Way3,
        Way4,
    };
    /// The orders every map has (the ways after them).
    inline constexpr int kOrderCount = 8;
    inline constexpr int kRadioWaysMost = 5;
    inline constexpr const char *kOrderNames[kOrderCount] = {"AUTO",    "GO A",      "GO B", "RUSH",
                                                             "SPLIT",   "DEFAULT",   "FOLLOW ME", "HOLD"};
    inline constexpr const char *kCtOrderNames[kOrderCount] = {"AUTO",   "BOMB A",    "BOMB B", "PUSH",
                                                               "SPREAD", "RETAKE",    "FOLLOW ME", "HOLD"};
    /// A team's place in what is kept of each (0: the terrorists, 1: the
    /// counter-terrorists).
    inline int teamSlot(std::uint8_t team) { return team == kTeamT ? 0 : 1; }
    /// The order each team's bots are under (this round's; AUTO at its
    /// start), a count of those given (a new one is one more), and who gave
    /// it -- whom FOLLOW ME follows: the player here (-2, the AI's
    /// kAiPlayer), or another console's in a match (their actor).
    constinit inline Order teamOrder[2] = {Order::Auto, Order::Auto};
    constinit inline int teamOrders[2] = {0, 0};
    constinit inline std::int16_t teamLeader[2] = {-2, -2};
    inline constexpr std::int16_t kOrderFromHere = -2; ///< (the AI's kAiPlayer: this console's player)
    inline void giveOrder(std::uint8_t team, Order order, std::int16_t leader = kOrderFromHere)
    {
        const int t = teamSlot(team);
        teamOrder[t] = order;
        teamLeader[t] = leader;
        ++teamOrders[t];
    }
    /// What the player said with it, for the game to play (CsSound; -1:
    /// nothing); and, in a match, the order to tell the host (-1: none).
    constinit inline int radioSay = -1;
    constinit inline int radioToSend = -1;
    /// This map's ways, as the radio names them: the terrorists' ways in
    /// (radioWay*), the counter-terrorists' places to hold (ctWay*).
    /// HlCsBots.hpp's applyKnownMap sets them; none on a map not known.
    constinit inline const char *radioWayName[kRadioWaysMost] = {};
    constinit inline int radioWays = 0;
    constinit inline const char *ctWayName[kRadioWaysMost] = {};
    constinit inline int ctWays = 0;
    /// The radio's cells: the orders, then the map's ways -- the player's
    /// team's.
    inline int radioCells();
    inline const char *orderName(int order);
    /// A match between consoles is being played (HlCsNet.hpp).
    constinit inline bool inMatch = false;
    /// A match's teams made up to this many with the host's bots (0: none;
    /// the lobby's; not kept).
    constinit inline int netTeamSize = 4;
    /// The choices of it: none, or the teams filled to 2, 3 or 4 a side
    /// (eight in all, as a match has seats).
    inline constexpr int kNetTeamSizes[] = {0, 2, 3, 4};
    inline void stepNetTeamSize(int step)
    {
        constexpr int count = static_cast<int>(sizeof(kNetTeamSizes) / sizeof(kNetTeamSizes[0]));
        int at = 0;
        for (int i = 0; i < count; ++i)
            if (kNetTeamSizes[i] == netTeamSize)
                at = i;
        netTeamSize = kNetTeamSizes[((at + (step < 0 ? -1 : 1)) % count + count) % count];
    }
    /// Which of their team's starts the player comes in at (a match's: by
    /// their seat; alone, the first).
    constinit inline int startIndex = 0;
    /// In a match: whether the player's team has bots of the host's in it
    /// (Game.arm.cpp, by the seats).
    constinit inline bool matchMates = false;
    /// Whether the player can give orders: either side's, with bots to take
    /// them.
    inline bool radioOffered() { return !inMatch || matchMates; }
    /// The player carries the bomb, where it may be put down (Game.arm.cpp
    /// says, each frame): the radio's last cell is then DROP BOMB -- the
    /// terrorist bots go for it (HlCsBots.hpp), a terrorist player walks
    /// over it -- and the radio is there for that if for nothing else.
    constinit inline bool bombInHand = false;
    /// ... and dropped, for the game to do (Game.arm.cpp).
    constinit inline bool bombDropAsked = false;
    inline bool radioOpen() { return radioOffered() || bombInHand; }
    inline int radioOrderCells()
    {
        if (!radioOffered())
            return 0;
        const int ways = player.team == kTeamT ? radioWays : ctWays;
        return kOrderCount + (ways < kRadioWaysMost ? ways : kRadioWaysMost);
    }
    /// DROP BOMB's cell (-1: none).
    inline int radioDropCell() { return bombInHand ? radioOrderCells() : -1; }
    inline int radioCells() { return radioOrderCells() + (bombInHand ? 1 : 0); }
    inline const char *orderName(int order)
    {
        if (order == radioDropCell())
            return "DROP BOMB";
        const bool t = player.team == kTeamT;
        if (order < kOrderCount)
            return t ? kOrderNames[order] : kCtOrderNames[order];
        const int way = order - kOrderCount;
        if (t)
            return way < radioWays && radioWayName[way] != nullptr ? radioWayName[way] : "";
        return way < ctWays && ctWayName[way] != nullptr ? ctWayName[way] : "";
    }

    struct BuyPanel
    {
        bool open = false;
        bool direct = false; ///< opened on the radio (out of a buy zone): BACK shuts it
        int menu = -1;  ///< the kind shown (Menu), -1 the first page
        int hover = -1; ///< the button under the stylus: a cell (0..kBuyMost-1), kBuyHoverBack, or -1
        bool owned = false; ///< the touch down now is the panel's
    };
    inline constexpr int kBuyHoverBack = 100;
    /// The radio's page (BuyPanel::menu).
    inline constexpr int kMenuRadio = 50;
    /// The first page's RADIO (a terrorist's), and its last cell: CLOSE.
    /// The first page: the kinds (kMenuNames), then 1.6's PRIMARY AMMO and
    /// SECONDARY AMMO, then RADIO (if there is anyone to order) and CLOSE.
    inline constexpr int kBuyPrimaryAmmoCell = kMenuCount, kBuySecondaryAmmoCell = kMenuCount + 1;
    inline constexpr int kBuyRadioCell = kMenuCount + 2;
    inline int buyCloseCell() { return kBuyRadioCell + (radioOpen() ? 1 : 0); }

    /// Cell `cell` of a page of `cells` (more than the five rows hold:
    /// the rows made shorter to fit, as the radio's on a map with ways).
    inline TouchRect buyCellRect(int cell, int cells = kBuyMost, int columns = kBuyColumns)
    {
        const int rows = (cells + columns - 1) / columns > kBuyRows ? (cells + columns - 1) / columns : kBuyRows;
        const int rowHeight = kBuyRows * kBuyRowHeight / rows;
        const int w = (kTouchWidth - kBuyInset * 2) / columns;
        const int col = cell % columns, row = cell / columns;
        return TouchRect{static_cast<std::int16_t>(kBuyInset + col * w),
                         static_cast<std::int16_t>(kBuyTop + kBuyHeadHeight + row * rowHeight),
                         static_cast<std::int16_t>(w - 2), static_cast<std::int16_t>(rowHeight - 2)};
    }
    inline TouchRect buyBackRect()
    {
        return TouchRect{kBuyInset, static_cast<std::int16_t>(kBuyTop + 1), kBuyBackWidth,
                         static_cast<std::int16_t>(kBuyHeadHeight - 2)};
    }
    /// The panel's whole area: what a touch there is the panel's.
    inline TouchRect buyPanelRect()
    {
        return TouchRect{0, static_cast<std::int16_t>(kBuyTop), kTouchWidth,
                         static_cast<std::int16_t>(kTouchPadHeight)};
    }
    /// The columns of page `menu`: the guns' long names one to a row, the
    /// equipment two.
    inline int buyColumns(int menu) { return menu >= 0 && menu < kMenuCount && menu != kMenuGear ? 1 : kBuyColumns; }
    /// How many cells the page shown has.
    inline int buyCells(const BuyPanel &panel)
    {
        if (panel.menu < 0)
            return buyCloseCell() + 1;
        if (panel.menu == kMenuRadio)
            return radioCells();
        int items[kBuyMost];
        return itemsOfMenu(panel.menu, items, kBuyMost, player.team);
    }
    /// The cell at (x, y): 0.., kBuyHoverBack (a kind's page's BACK), or -1.
    inline int buyCellAt(const BuyPanel &panel, int x, int y)
    {
        if (panel.menu >= 0 && buyBackRect().has(x, y))
            return kBuyHoverBack;
        const int cells = buyCells(panel);
        for (int c = 0; c < cells; ++c)
        {
            if (buyCellRect(c, cells, buyColumns(panel.menu)).has(x, y))
                return c;
        }
        return -1;
    }

    /// What a frame of the stylus did to the panel.
    struct BuyTouch
    {
        bool consumed = false; ///< the touch was the panel's: not the controls'
        int item = kNoItem;    ///< an item chosen (kItems)
        int order = -1;        ///< an order given (Order)
        int ammo = -1;         ///< PRIMARY AMMO (kSlotPrimary) or SECONDARY AMMO (kSlotPistol) chosen
        bool dropBomb = false; ///< DROP BOMB
        bool moved = false;    ///< onto another button
        bool paged = false;    ///< another page shown (or the panel shut)
    };

    /// The stylus, each frame, while the panel is up: a button is chosen as
    /// the stylus is lifted off it (as the menus' are), so a finger can slide
    /// off one it did not mean.
    inline BuyTouch buyPanelStep(BuyPanel &panel, bool touching, bool landed, int x, int y)
    {
        BuyTouch out;
        if (!panel.open)
            return out;
        if (landed)
            panel.owned = buyPanelRect().has(x, y);
        if (touching)
        {
            if (!panel.owned)
                return out;
            out.consumed = true;
            const int at = buyCellAt(panel, x, y);
            out.moved = at != panel.hover && at >= 0;
            panel.hover = at;
            return out;
        }
        // Lifted: whatever it was lifted from.
        if (!panel.owned)
            return out;
        panel.owned = false;
        out.consumed = true;
        const int chosen = panel.hover;
        panel.hover = -1;
        if (chosen < 0)
            return out;
        if (chosen == kBuyHoverBack)
        {
            panel.menu = -1;
            if (panel.direct)
                panel.open = false;
            out.paged = true;
            return out;
        }
        if (panel.menu < 0)
        {
            out.paged = true;
            if (chosen == buyCloseCell())
                panel.open = false;
            else if (chosen == kBuyPrimaryAmmoCell || chosen == kBuySecondaryAmmoCell)
            {
                // (Bought there and then: the page stays.)
                out.ammo = chosen == kBuyPrimaryAmmoCell ? kSlotPrimary : kSlotPistol;
                out.paged = false;
            }
            else if (chosen == kBuyRadioCell && radioOpen())
                panel.menu = kMenuRadio;
            else
                panel.menu = chosen;
            return out;
        }
        if (panel.menu == kMenuRadio)
        {
            if (chosen == radioDropCell())
                out.dropBomb = true;
            else if (chosen < radioOrderCells())
                out.order = chosen;
            panel.open = false;
            out.paged = true;
            return out;
        }
        int items[kBuyMost];
        const int n = itemsOfMenu(panel.menu, items, kBuyMost, player.team);
        if (chosen < n)
            out.item = items[chosen];
        return out;
    }

    inline void openBuy(BuyPanel &panel)
    {
        panel = BuyPanel();
        panel.open = true;
    }
    inline void openRadio(BuyPanel &panel)
    {
        panel = BuyPanel();
        panel.open = true;
        panel.direct = true;
        panel.menu = kMenuRadio;
    }
} // namespace hldata::cs
