// Counter-Strike's rules (HlCs.hpp): the setup and its file, the buy menu --
// what it sells, to whom, for how much, and what the player has after --
// where the player may buy, and the touch screen's buy panel.

#include <cstdio>
#include <cstring>
#include <vector>

#include <memory>
#include <string>

#include "HlCs.hpp"
#include "HlCsNet.hpp"
#include "HlCsRound.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldata::cs;
using namespace hldstest;

namespace
{
    int itemNamed(const char *name)
    {
        for (int i = 0; i < kItemCount; ++i)
        {
            if (std::strcmp(kItems[i].name, name) == 0)
                return i;
        }
        return kNoItem;
    }

    void testSetup()
    {
        Setup s;
        setMap(s, "de_nuke");
        s.tBots = 3;
        s.ctBots = 5;
        s.botSkill = 3;
        s.team = kTeamT;
        s.money = 2;
        s.roundMinutes = 5;
        s.faction = 2;
        std::uint8_t bytes[kSetupBytes];
        check(writeSetup(s, bytes) == kSetupBytes, "the setup is written whole");
        Setup back;
        check(readSetup(bytes, sizeof(bytes), back) && std::strcmp(back.map, "de_nuke") == 0 && back.tBots == 3 && back.ctBots == 5 &&
                  back.botSkill == 3 && back.team == kTeamT && back.money == 2 && back.roundMinutes == 5 && back.faction == 2,
              "and read back as it was");
        bytes[4] = 99;
        Setup other;
        check(!readSetup(bytes, sizeof(bytes), other) && other.tBots == 2 && other.ctBots == 2, "another version is not read");
        writeSetup(s, bytes);
        bytes[5] = 200;  // terrorist bots
        bytes[11] = 9;   // counter-terrorist bots
        bytes[12] = 77;  // the match's length
        bytes[13] = '/'; // the map's name
        check(readSetup(bytes, sizeof(bytes), back) && back.tBots == 2 && back.ctBots == 2 && back.map[0] == '\0',
              "nonsense in the file is not believed");

        // Each row steps round.
        Setup r;
        r.tBots = kBotSlotsPerSide;
        nextTBots(r);
        check(r.tBots == 0, "terrorist bots: after the most, none");
        nextCTBots(r);
        check(r.ctBots == 3, "counter-terrorist bots step on their own");
        r.botSkill = kBotSkills - 1;
        nextBotSkill(r);
        check(r.botSkill == 0, "skill: round");
        nextTeam(r);
        check(r.team == kTeamT, "team: CT then T");
        r.faction = kFactions - 1;
        nextFaction(r);
        check(r.faction == 0 && std::strcmp(factionName(kTeamT, 1), "Elite Crew") == 0 &&
                  std::strcmp(factionModel(kTeamCT, 3), "gign") == 0,
              "four factions a side, round");
        r.money = kStartMoneyCount - 1;
        nextMoney(r);
        check(startMoney(r) == 800, "money: round to $800");
        r.roundMinutes = kRoundMinutesMost;
        nextRoundMinutes(r);
        check(r.roundMinutes == kRoundMinutesLeast, "round time: round");
        const char *const maps[] = {"de_dust2", "de_inferno", "de_nuke"};
        setMap(r, "de_inferno");
        check(std::strcmp(nextMap(r, maps, 3), "de_nuke") == 0, "map: the next on the card");
        setMap(r, "de_nuke");
        check(std::strcmp(nextMap(r, maps, 3), "de_dust2") == 0, "map: round");
        setMap(r, "cs_gone");
        check(std::strcmp(nextMap(r, maps, 3), "de_dust2") == 0, "map: one not on the card, the first");
        setMap(r, "a_name_much_too_long_for_it");
        check(std::strlen(r.map) == static_cast<std::size_t>(kMapNameMost), "a long name is cut");
    }

    void testCatalogue()
    {
        // Every kind has something, and fits its page.
        for (int m = 0; m < kMenuCount; ++m)
            for (const std::uint8_t team : {kTeamT, kTeamCT})
            {
                int items[kBuyMost + 4];
                const int n = itemsOfMenu(m, items, kBuyMost + 4, team);
                check(n > 0 && n <= kBuyMost, "every kind fits its page");
                // (Each a team's own, named as 1.6 names it, and room for its
                // name and price in its cell: 8 pixels a letter.)
                for (int c = 0; c < n; ++c)
                {
                    const Item &it = kItems[items[c]];
                    const TouchRect r = buyCellRect(c, n, buyColumns(m));
                    const int name = static_cast<int>(std::strlen(it.label)) * 8, price = 5 * 8;
                    check((it.teams & (1u << team)) != 0, "only the team's own on its page");
                    check(buyColumns(m) == 1 ? 2 + name + 2 + price + 2 <= r.w : 2 + name <= r.w, it.label);
                }
            }
        {
            int rifles[kBuyMost];
            const int n = itemsOfMenu(kMenuRifles, rifles, kBuyMost, kTeamCT);
            check(n == 6 && std::strcmp(kItems[rifles[0]].label, "CLARION 5.56") == 0 &&
                      std::strcmp(kItems[rifles[2]].label, "MAVERICK M4A1 CARBINE") == 0 &&
                      std::strcmp(kItems[rifles[5]].label, "MAGNUM SNIPER RIFLE") == 0,
                  "the counter-terrorists' rifles, in 1.6's order and names");
            int gear[kBuyMost];
            check(itemsOfMenu(kMenuGear, gear, kBuyMost, kTeamT) == 6 && itemsOfMenu(kMenuGear, gear, kBuyMost, kTeamCT) == 8,
                  "the equipment: six for a terrorist, eight for a counter-terrorist");
        }
        const int ak = itemNamed("AK-47"), m4 = itemNamed("M4A1"), awp = itemNamed("AWP"), kit = itemNamed("DEFUSE KIT");
        check(ak != kNoItem && kItems[ak].price == 2500 && kItems[ak].teams == kTeamsT, "the AK is the terrorists'");
        check(m4 != kNoItem && kItems[m4].price == 3100 && kItems[m4].teams == kTeamsCT, "the M4 the CTs'");
        check(awp != kNoItem && kItems[awp].teams == kTeamsBoth && kItems[awp].standIn == kWeaponCrossbow,
              "the AWP anyone's, with a sight");
        check(kit != kNoItem && kItems[kit].teams == kTeamsCT, "the kit the CTs'");
        for (int i = 0; i < kItemCount; ++i)
        {
            const Item &it = kItems[i];
            check(it.price > 0 && it.teams != 0 && std::strlen(it.name) <= 13, "each item a price, a team, a short name");
            check(it.gear != kGearNone || it.standIn != kWeaponNone, "each weapon stands in as one of the game's");
        }
    }

    void testBuying()
    {
        PlayerState state;
        Arsenal arsenal;
        state.arsenal = &arsenal;
        PlayerCs cs;
        Setup s;
        s.team = kTeamT;
        startLoadout(state, cs, s);
        check(cs.money == 800 && cs.team == kTeamT, "a terrorist with $800");
        check(hasWeapon(state, kWeaponCrowbar) && hasWeapon(state, kWeaponGlock) && state.inv.current == kWeaponGlock,
              "a knife and a pistol in hand");
        check((state.inv.have & 1u) != 0, "and the suit (the HUD)");

        std::uint8_t hand = 0;
        const int ak = itemNamed("AK-47"), m4 = itemNamed("M4A1"), deagle = itemNamed("DESERT EAGLE");
        check(buy(state, cs, ak, hand) == Buy::NotHere && cs.money == 800, "nothing bought outside a buy zone");
        cs.inBuyZone = true;
        check(buy(state, cs, ak, hand) == Buy::NoMoney && cs.money == 800, "an AK is too dear at $800");
        cs.money = 5000;
        check(buy(state, cs, m4, hand) == Buy::OtherTeam && cs.money == 5000, "an M4 is not a terrorist's");
        const int glockSpare = state.inv.ammo9mm;
        check(buy(state, cs, ak, hand) == Buy::Ok && cs.money == 2500 && hand == kWeaponMp5 &&
                  hasWeapon(state, kWeaponMp5) && state.inv.clipMp5 == 30 && state.inv.ammo9mm == 0 && state.inv.ammoAr == 0 &&
                  cs.spare[kSlotPrimary] == 0 && cs.spare[kSlotPistol] == glockSpare,
              "an AK bought: $2500 less, its 30 in and none besides, as 1.6's, to hand; the Glock's kept");
        // PRIMARY AMMO: 7.62 at $80 for 30, until 90.
        check(buyAmmo(state, cs, kSlotPrimary) == Buy::Ok && cs.money == 2500 - 3 * 80 && state.inv.ammo9mm == 90 &&
                  cs.spare[kSlotPrimary] == 90 && buyAmmo(state, cs, kSlotPrimary) == Buy::Have,
              "PRIMARY AMMO: three boxes of 7.62, $240, to the AK's 90");
        // The Glock taken out: its own spare rounds in the pool the two share.
        state.inv.current = kWeaponGlock;
        ammoSync(state, cs);
        const bool glockHas = state.inv.ammo9mm == glockSpare;
        state.inv.ammo9mm = static_cast<std::int16_t>(glockSpare - 20); // (two magazines fired and reloaded)
        state.inv.current = kWeaponMp5;
        ammoSync(state, cs);
        check(glockHas && state.inv.ammo9mm == 90 && cs.spare[kSlotPistol] == glockSpare - 20,
              "the Glock's and the AK's spare rounds each their own, in the pool they share");
        cs.money = 2500;
        check(buy(state, cs, ak, hand) == Buy::Have && cs.money == 2500, "not the same gun twice");

        const int awp = itemNamed("AWP");
        cs.money = 16000;
        check(buy(state, cs, awp, hand) == Buy::Ok && !hasWeapon(state, kWeaponMp5) && hasWeapon(state, kWeaponCrossbow) &&
                  arsenal.boltsClip == 10 && arsenal.bolts == 0,
              "an AWP takes the AK's place, its 10 and none more");
        // A gun on the floor: walked over with the slot taken, left; with
        // USE, taken, the one carried let fall in its place.
        {
            clearFloorGuns();
            const fx middle[3] = {0, 0, fxFromInt(36)};
            const fx floor[3] = {fxFromInt(10), 0, 0};
            dropGun(floor, itemNamed("M4A1"));
            std::uint8_t took = kWeaponNone;
            check(!pickUpTick(state, cs, middle, false, took), "a gun lying is not taken over the one carried");
            check(pickUpTick(state, cs, middle, true, took) && cs.primary == itemNamed("M4A1") && took == kWeaponMp5 &&
                      hasWeapon(state, kWeaponMp5) && !hasWeapon(state, kWeaponCrossbow) && state.inv.clipMp5 == 30,
                  "USE takes it, loaded");
            int lying = 0;
            for (const FloorGun &g : floorGuns)
                lying += g.active ? 1 : 0;
            check(lying == 0, "the AWP is not left lying (it has no floor model), the M4 taken");
            dropGun(floor, itemNamed("AK-47"));
            cs.primary = kNoItem;
            dropSlot(state, kSlotPrimary);
            check(pickUpTick(state, cs, middle, false, took) && cs.primary == itemNamed("AK-47"),
                  "walked over with nothing in its slot: taken");
            check(iconFor(kWeaponMp5, cs) == 1 + 15 && iconFor(kWeaponCrowbar, cs) == kCsIconKnife,
                  "the AK's picture, the knife's");
        }
        // The match: first past 13, or 13-13 a draw.
        check(matchEnd(13, 12, 13) == MatchEnd::On && matchEnd(14, 12, 13) == MatchEnd::TerroristsWin &&
                  matchEnd(12, 14, 13) == MatchEnd::CounterTerroristsWin && matchEnd(13, 13, 13) == MatchEnd::Draw &&
                  matchEnd(40, 2, 0) == MatchEnd::On,
              "a match's end");
        // Grenades: an HE and two flashbangs, the one bought last in hand.
        {
            const int he = itemNamed("HE GRENADE"), flash = itemNamed("FLASHBANG");
            // (On copies: what follows buys its own.)
            PlayerState state = PlayerState();
            PlayerCs cs;
            cs.team = kTeamT;
            cs.inBuyZone = true;
            cs.money = 16000;
            const Buy b = buy(state, cs, he, hand);
            check(b == Buy::Ok && cs.he == 1 && state.inv.grenades == 1 && grenadeInHand(cs) == kGearHe,
                  "an HE bought");
            check(buy(state, cs, he, hand) == Buy::Have, "one HE only");
            check(buy(state, cs, flash, hand) == Buy::Ok && buy(state, cs, flash, hand) == Buy::Ok && cs.flashes == 2 &&
                      state.inv.grenades == 3 && grenadeInHand(cs) == kGearFlash,
                  "two flashbangs, in hand");
            check(buy(state, cs, flash, hand) == Buy::Have, "two flashbangs only");
            check(std::strcmp(viewModelFor(kWeaponGrenade, cs, false), "v_flashbang") == 0, "a flashbang held as one");
            // ... a smoke grenade; and nothing of the later games' fire.
            const int smoke = itemNamed("SMOKE GRENADE");
            check(smoke >= 0 && itemNamed("MOLOTOV") < 0 && itemNamed("INCENDIARY") < 0, "smoke on sale, as 1.6's");
            check(buy(state, cs, smoke, hand) == Buy::Ok && cs.smokes == 1 && grenadeInHand(cs) == kGearSmoke &&
                      state.inv.grenades == 4 && std::strcmp(viewModelFor(kWeaponGrenade, cs, false), "v_smokegrenade") == 0,
                  "a smoke grenade bought, in hand, held as one");
            check(buy(state, cs, smoke, hand) == Buy::Have, "one smoke only");
            // Taken again: the next kind round, the HE after the smoke.
            check(nextGrenade(cs) && grenadeInHand(cs) == kGearHe && iconFor(kWeaponGrenade, cs) == kCsIconHe,
                  "the grenades taken again: the HE");
            check(nextGrenade(cs) && grenadeInHand(cs) == kGearFlash && nextGrenade(cs) && grenadeInHand(cs) == kGearSmoke &&
                      iconFor(kWeaponGrenade, cs) == kCsIconSmoke,
                  "... the flashbang, the smoke");
            // The last of a kind gone: the next in hand.
            cs.smokes = 0;
            grenadeGone(cs, kGearSmoke);
            check(grenadeInHand(cs) == kGearHe, "the smoke thrown: the HE in hand");
        }
        // Every gun has Counter-Strike's numbers.
        for (int i = 0; i < kItemCount; ++i)
        {
            if (kItems[i].slot == kSlotPrimary || kItems[i].slot == kSlotPistol)
                check(gunOfItem(i) != nullptr, kItems[i].name);
        }
        check(buy(state, cs, deagle, hand) == Buy::Ok && !hasWeapon(state, kWeaponGlock) && hasWeapon(state, kWeaponPython) &&
                  hand == kWeaponPython,
              "a Desert Eagle the pistol's");
        check(hasWeapon(state, kWeaponCrowbar), "the knife is never dropped");

        const int kevlar = itemNamed("KEVLAR"), helmet = itemNamed("KEVLAR+HELMET"), he = itemNamed("HE GRENADE");
        int before = cs.money;
        check(buy(state, cs, kevlar, hand) == Buy::Ok && state.inv.armor == 100 && hand == kWeaponNone &&
                  cs.money == before - 650,
              "kevlar: 100 armour");
        check(buy(state, cs, kevlar, hand) == Buy::Have, "and not again while it is whole");
        before = cs.money;
        check(buy(state, cs, helmet, hand) == Buy::Ok && cs.helmet && cs.money == before - 350,
              "the helmet over kevlar costs its part");
        check(buy(state, cs, he, hand) == Buy::Ok && state.inv.grenades == 1, "an HE");
        check(buy(state, cs, he, hand) == Buy::Have, "one HE at a time");
        check(buy(state, cs, itemNamed("DEFUSE KIT"), hand) == Buy::OtherTeam, "no kit for a terrorist");
    }

    void testZones()
    {
        // A map with two starts each, and a counter-terrorists' buy zone.
        std::vector<EntityRecord> entities(4);
        entities[0].classId = static_cast<std::uint16_t>(EntityClass::InfoPlayerStart);
        entities[0].origin[0] = fxFromInt(1000);
        entities[1].classId = static_cast<std::uint16_t>(EntityClass::InfoPlayerStart);
        entities[1].origin[0] = fxFromInt(1100);
        entities[2].classId = static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch);
        entities[2].origin[0] = fxFromInt(-1000);
        entities[3].classId = static_cast<std::uint16_t>(EntityClass::InfoPlayerDeathmatch);
        entities[3].origin[0] = fxFromInt(-1100);
        LevelView view;
        view.entities = entities.data();
        view.entityCount = static_cast<std::uint32_t>(entities.size());

        const EntityRecord *at = nullptr;
        check(teamStart(view, kTeamCT, 0, at) && at == &entities[0], "the first CT start");
        check(teamStart(view, kTeamCT, 3, at) && at == &entities[1], "counted round");
        check(teamStart(view, kTeamT, 0, at) && at == &entities[2], "the terrorists' are the deathmatch starts");

        // No buy zones: about the team's own starts.
        const fx nearT[3] = {fxFromInt(-1050), fxFromInt(50), 0};
        const fx far[3] = {fxFromInt(0), 0, 0};
        check(inBuyZone(view, kTeamT, nearT) && !inBuyZone(view, kTeamCT, nearT), "no zones: round a team's starts");
        check(!inBuyZone(view, kTeamT, far), "and not in the middle");

        LogicRecord zone{};
        zone.kind = static_cast<std::uint8_t>(LogicKind::BuyZone);
        zone.extra[0] = kTeamCT;
        zone.mins[0] = 900;
        zone.maxs[0] = 1200;
        zone.mins[1] = -100;
        zone.maxs[1] = 100;
        zone.mins[2] = -50;
        zone.maxs[2] = 200;
        view.logic = &zone;
        view.logicCount = 1;
        const fx inZone[3] = {fxFromInt(1000), 0, 0};
        check(inBuyZone(view, kTeamCT, inZone) && !inBuyZone(view, kTeamT, inZone), "a zone is its team's");
        check(!inBuyZone(view, kTeamT, nearT), "a map with zones has no others");
        zone.extra[0] = 0;
        check(inBuyZone(view, kTeamT, inZone), "a zone of no team is both's");
    }

    void testPanel()
    {
        BuyPanel panel;
        check(!buyPanelStep(panel, true, true, 100, 100).consumed, "a shut panel takes nothing");
        openBuy(panel);
        // RIFLES is the fourth cell: lifted from it, its page.
        const TouchRect rifles = buyCellRect(kMenuRifles);
        const int rx = rifles.x + 4, ry = rifles.y + 4;
        check(buyPanelStep(panel, true, true, rx, ry).consumed && panel.hover == kMenuRifles, "pressed on RIFLES");
        const BuyTouch up = buyPanelStep(panel, false, false, 0, 0);
        check(up.consumed && up.paged && panel.menu == kMenuRifles, "lifted: the rifles' page");
        // The first rifle bought by its cell.
        // (One to a row, the team's own: a counter-terrorist's first is the
        // Clarion, the FAMAS.)
        const TouchRect first = buyCellRect(0, buyCells(panel), buyColumns(kMenuRifles));
        buyPanelStep(panel, true, true, first.x + 2, first.y + 2);
        const BuyTouch got = buyPanelStep(panel, false, false, 0, 0);
        check(got.item == itemNamed(player.team == kTeamT ? "GALIL" : "FAMAS"), "the first rifle chosen");
        // Slid off before lifting: nothing.
        buyPanelStep(panel, true, true, first.x + 2, first.y + 2);
        buyPanelStep(panel, true, false, first.x + 2, kBuyTop - 5);
        check(buyPanelStep(panel, false, false, 0, 0).item == kNoItem, "slid off: nothing bought");
        // BACK, then CLOSE.
        const TouchRect back = buyBackRect();
        buyPanelStep(panel, true, true, back.x + 2, back.y + 2);
        buyPanelStep(panel, false, false, 0, 0);
        check(panel.open && panel.menu == -1, "BACK: the first page");
        const TouchRect close = buyCellRect(buyCloseCell());
        buyPanelStep(panel, true, true, close.x + 2, close.y + 2);
        buyPanelStep(panel, false, false, 0, 0);
        check(!panel.open, "CLOSE shuts it");
        // PRIMARY AMMO on the first page: chosen, the page stays.
        openBuy(panel);
        const TouchRect primAmmo = buyCellRect(kBuyPrimaryAmmoCell);
        buyPanelStep(panel, true, true, primAmmo.x + 2, primAmmo.y + 2);
        const BuyTouch ammoChosen = buyPanelStep(panel, false, false, 0, 0);
        check(ammoChosen.ammo == kSlotPrimary && panel.open && panel.menu == -1, "PRIMARY AMMO chosen, the page kept");
        panel.open = false;
        // RADIO on the first page, its orders -- either side's, their own;
        // and opened on the radio (out of a buy zone), BACK shuts it.
        {
            const std::uint8_t was = player.team;
            player.team = kTeamCT;
            check(buyCloseCell() == kBuyRadioCell + 1, "a counter-terrorist's: RADIO too");
            check(std::string(orderName(static_cast<int>(Order::GoA))) == "BOMB A" &&
                      std::string(orderName(static_cast<int>(Order::Default))) == "RETAKE",
                  "a counter-terrorist's orders as theirs");
            // (In a match with no bots on their side: none -- but the bomb's
            // DROP BOMB, carried.)
            inMatch = true;
            matchMates = false;
            check(buyCloseCell() == kBuyRadioCell && !radioOpen(), "a match, no bots of theirs: no RADIO");
            player.team = kTeamT;
            bombInHand = true;
            check(radioOpen() && radioCells() == 1 && radioDropCell() == 0 &&
                      std::string(orderName(0)) == "DROP BOMB",
                  "... but the bomb carried: RADIO, its one cell DROP BOMB");
            openRadio(panel);
            const TouchRect drop = buyCellRect(0, 1);
            buyPanelStep(panel, true, true, drop.x + 2, drop.y + 2);
            check(buyPanelStep(panel, false, false, 0, 0).dropBomb, "DROP BOMB chosen");
            inMatch = false;
            check(radioCells() == kOrderCount + 1 && radioDropCell() == kOrderCount, "alone: the orders, then DROP BOMB");
            bombInHand = false;
            check(buyCloseCell() == kBuyRadioCell + 1 && buyCloseCell() < kBuyMost, "a terrorist's: RADIO, then CLOSE");
            openBuy(panel);
            const TouchRect radio = buyCellRect(kBuyRadioCell);
            buyPanelStep(panel, true, true, radio.x + 2, radio.y + 2);
            buyPanelStep(panel, false, false, 0, 0);
            check(panel.open && panel.menu == kMenuRadio && buyCells(panel) == kOrderCount, "RADIO: the orders");
            const TouchRect goA = buyCellRect(static_cast<int>(Order::GoA));
            buyPanelStep(panel, true, true, goA.x + 2, goA.y + 2);
            const BuyTouch told = buyPanelStep(panel, false, false, 0, 0);
            check(told.order == static_cast<int>(Order::GoA) && told.item == kNoItem && !panel.open,
                  "GO A given: the panel shut");
            openRadio(panel);
            check(panel.open && panel.direct && panel.menu == kMenuRadio, "opened on the radio");
            buyPanelStep(panel, true, true, back.x + 2, back.y + 2);
            buyPanelStep(panel, false, false, 0, 0);
            check(!panel.open, "its BACK shuts it");
            check(kOrderCount <= kBuyMost, "the orders fit the page");
            // (A map with ways: more cells than five rows hold -- shorter
            // rows, all on the panel, none over another.)
            radioWays = 3;
            radioWayName[0] = "OUTSIDE SECRET";
            radioWayName[1] = "VENT";
            radioWayName[2] = "RAMP";
            const int cells = radioCells();
            bool fits = cells == kOrderCount + 3;
            for (int c = 0; c < cells; ++c)
            {
                const TouchRect r = buyCellRect(c, cells);
                fits = fits && r.y + r.h <= kBuyTop + kTouchPadHeight && r.h >= 12;
                if (c + kBuyColumns < cells)
                    fits = fits && buyCellRect(c + kBuyColumns, cells).y >= r.y + r.h;
            }
            check(fits, "the radio's eleven cells fit the panel");
            check(std::string(orderName(kOrderCount + 1)) == "VENT", "a way named");
            openRadio(panel);
            const TouchRect vent = buyCellRect(kOrderCount + 1, cells);
            buyPanelStep(panel, true, true, vent.x + 2, vent.y + 2);
            const BuyTouch way = buyPanelStep(panel, false, false, 0, 0);
            check(way.order == static_cast<int>(Order::Way1), "VENT given");
            radioWays = 0;
            player.team = was;
        }
        // A touch that began outside it (the bar, the slots) is not its.
        openBuy(panel);
        check(!buyPanelStep(panel, true, true, 10, 5).consumed, "a touch on the bar is the bar's");
        // Every cell inside the panel, none over another.
        for (int c = 0; c < kBuyMost; ++c)
        {
            const TouchRect r = buyCellRect(c);
            check(buyPanelRect().has(r.x, r.y) && buyPanelRect().has(r.x + r.w - 1, r.y + r.h - 1),
                  "each cell inside the panel");
            check(!r.has(buyBackRect().x, buyBackRect().y), "none under BACK");
        }
    }
    /// Frames of `ms` until `total` has gone by.
    RoundOut run(Round &r, RoundIn in, PlayerCs &cs, int total, int ms = 50)
    {
        RoundOut seen;
        for (int t = 0; t < total; t += ms)
        {
            in.ms = ms;
            const RoundOut o = roundTick(r, in, cs);
            seen.started |= o.started;
            seen.unfrozen |= o.unfrozen;
            seen.planted |= o.planted;
            seen.exploded |= o.exploded;
            seen.defused |= o.defused;
            seen.ended |= o.ended;
            seen.beep |= o.beep;
            seen.blast = o.blast > seen.blast ? o.blast : seen.blast;
        }
        return seen;
    }

    void testRounds()
    {
        Setup s;
        s.team = kTeamT;
        s.roundMinutes = 2;
        Round r;
        PlayerCs cs;
        cs.team = kTeamT;
        cs.money = 800;
        newGame(r, s, 0, true);
        check(r.phase == Phase::Freeze && r.hasBomb && r.bombRadius == kBombRadiusDefault, "round 1: frozen, the T has the bomb");
        check(buyTime(r), "buying in the freeze");
        const fx site[3] = {fxFromInt(500), fxFromInt(500), 0};
        RoundIn in;
        in.team = kTeamT;
        in.origin = site;
        RoundOut o = run(r, in, cs, kFreezeMs);
        check(o.unfrozen && r.phase == Phase::Live && r.clockMs == 2 * 60000, "then live, on a two-minute clock");
        in.inBombSite = false;
        check(actionOf(r, in) == Action::Buy, "out of a site: BUY");
        in.inBombSite = true;
        check(actionOf(r, in) == Action::Buy, "in a site: the button is still BUY (the bomb plants from its slot)");
        // Planting held for less than its time, then let go: starts over.
        in.actionHeld = true;
        run(r, in, cs, kPlantMs / 2);
        in.actionHeld = false;
        run(r, in, cs, 100);
        check(r.phase == Phase::Live && r.plantMs == 0, "let go: no bomb, the plant starts over");
        in.actionHeld = true;
        o = run(r, in, cs, kPlantMs + 100);
        check(o.planted && r.phase == Phase::Planted && !r.hasBomb && r.bomb[0] == site[0], "held: planted where they stand");
        in.actionHeld = false;
        // Walk away and let it go off.
        const fx away[3] = {fxFromInt(500 + 5000), fxFromInt(500), 0};
        in.origin = away;
        o = run(r, in, cs, kC4Ms + 100);
        check(o.beep && o.exploded && o.blast == 0 && r.phase == Phase::Over && r.winner == kTeamT &&
                  r.why == Why::TargetBombed && r.scoreT == 1,
              "it beeps, goes off: the terrorists' round (and far away, no hurt)");
        check(cs.money == 800 + kWinObjective, "the bomb's money");
        o = run(r, in, cs, kRoundOverMs);
        check(o.started && r.phase == Phase::Freeze && r.hasBomb && r.number == 1, "the next round, the bomb again");

        // Standing on it: deadly.
        check(blastAt(500, 0) == 500 && blastAt(500, 1750) == 0 && blastAt(500, 875) == 250,
              "the blast falls off evenly to nothing, as 1.6's");

        // The clock runs out: the CTs save the target; the T loses $1400, then more.
        run(r, in, cs, kFreezeMs);
        in.origin = away;
        in.inBombSite = false;
        const int before = cs.money;
        o = run(r, in, cs, 2 * 60000 + 100);
        check(o.ended && r.winner == kTeamCT && r.why == Why::TargetSaved && cs.money == before + kLossFirst,
              "out of time: the target saved, and the loss bonus");
        check(lossBonus(1) == 1400 && lossBonus(2) == 1900 && lossBonus(5) == 3400 && lossBonus(9) == 3400,
              "the loss bonus grows to $3400");
        check(!buyTime(r) || r.phase == Phase::Over, "");

        // Dying alone ends it for the other side.
        run(r, in, cs, kRoundOverMs + kFreezeMs);
        in.alive = false;
        o = run(r, in, cs, 100);
        check(o.ended && r.winner == kTeamCT && r.why == Why::TerroristsDead, "the last terrorist dead: CTs win");

        // A CT defusing: with a kit, five seconds; without, ten.
        Setup ct;
        ct.team = kTeamCT;
        Round d;
        PlayerCs cop;
        newGame(d, ct, 0, true);
        check(!d.hasBomb, "a CT has no bomb");
        d.phase = Phase::Planted;
        d.bombMs = kC4Ms;
        d.bomb[0] = fxFromInt(100);
        RoundIn def;
        def.team = kTeamCT;
        const fx by[3] = {fxFromInt(140), 0, 0};
        def.origin = by;
        check(actionOf(d, def) == Action::Defuse, "by the bomb: DEFUSE");
        const fx far2[3] = {fxFromInt(400), 0, 0};
        def.origin = far2;
        check(actionOf(d, def) == Action::Buy, "far from it: not");
        def.origin = by;
        def.actionHeld = true;
        def.hasKit = true;
        o = run(d, def, cop, kDefuseKitMs + 100);
        check(o.defused && d.winner == kTeamCT && d.why == Why::BombDefused, "a kit: defused in five seconds");

        // Buy time: 90 s after the freeze.
        Round b;
        newGame(b, ct, 0, true);
        RoundIn idle;
        idle.origin = by;
        run(b, idle, cop, kFreezeMs + kBuyMs - 1000);
        check(buyTime(b), "still buying near the end of buy time");
        run(b, idle, cop, 2000);
        check(!buyTime(b), "and not after it");
    }

    void testCarry()
    {
        PlayerState state;
        Arsenal arsenal;
        state.arsenal = &arsenal;
        PlayerCs cs;
        Setup s;
        s.team = kTeamCT;
        startLoadout(state, cs, s);
        cs.money = 9000;
        cs.inBuyZone = true;
        std::uint8_t hand = 0;
        buy(state, cs, itemNamed("M4A1"), hand);
        // Night vision, as 1.6 sells it: $1250, one pair; on and off.
        const int nvg = itemNamed("NIGHTVISION");
        const int moneyWas = cs.money;
        check(nvg >= 0 && kItems[nvg].price == 1250 && buy(state, cs, nvg, hand) == Buy::Ok && cs.nvg &&
                  cs.money == moneyWas - 1250 && buy(state, cs, nvg, hand) == Buy::Have && nvgSaid == 3,
              "night vision bought, once, for $1250");
        nvgToggle(cs);
        const bool on = cs.nvgOn && nvgSaid == 1;
        nvgToggle(cs);
        check(on && !cs.nvgOn && nvgSaid == 2, "night vision on, and off");
        nvgSaid = 0;
        // The tactical shield: a counter-terrorist's, $2200, in place of the
        // primary -- and let go of for one; held with the pistol as its own
        // model; raised, slower.
        {
            PlayerState ss;
            Arsenal sa;
            ss.arsenal = &sa;
            PlayerCs sc;
            Setup st;
            st.team = kTeamCT;
            startLoadout(ss, sc, st);
            sc.money = 16000;
            sc.inBuyZone = true;
            std::uint8_t h = 0;
            const int shield = itemNamed("SHIELD");
            buy(ss, sc, itemNamed("M4A1"), h);
            check(shield >= 0 && kItems[shield].price == 2200 && buy(ss, sc, shield, h) == Buy::Ok && sc.shield &&
                      sc.primary == kNoItem && !hasWeapon(ss, kWeaponMp5) && buy(ss, sc, shield, h) == Buy::Have,
                  "the shield bought, for $2200: the M4 let go of");
            check(std::strcmp(viewModelFor(kWeaponGlock, sc, false), "shield/v_shield_usp") == 0 &&
                      std::strcmp(viewModelFor(kWeaponCrowbar, sc, false), "shield/v_shield_knife") == 0 &&
                      viewNamed("shield/v_shield_usp") != nullptr,
                  "the USP and the knife held with the shield");
            check(speedFor(kWeaponGlock, sc, false) == 250, "with the shield lowered: a pistol's 250");
            sc.shieldUp = true;
            check(speedFor(kWeaponGlock, sc, false) == 180, "raised: 180");
            // What it stops: from in front, not from behind; the head only raised.
            const PlayerCs keep = player;
            player = sc;
            ss.health = 100;
            ss.origin[0] = ss.origin[1] = ss.origin[2] = 0;
            ss.facingXY[0] = kFxOne;
            ss.facingXY[1] = 0;
            const fx ahead[3] = {fxFromInt(500), fxFromInt(100), 0}, behind[3] = {-fxFromInt(500), 0, 0};
            const fx wide[3] = {fxFromInt(300), fxFromInt(400), 0};
            const bool upFront = shieldBlocks(ss, ahead, 1) && shieldBlocks(ss, wide, 0) && !shieldBlocks(ss, behind, 0);
            player.shieldUp = false;
            const bool downFront = shieldBlocks(ss, ahead, 0) && !shieldBlocks(ss, ahead, 1) && !shieldBlocks(ss, wide, 0);
            check(upFront && downFront, "the shield stops blows from in front: the head, and wider, only raised");
            player = keep;
            sc.shieldUp = false;
            buy(ss, sc, itemNamed("AK-47"), h);
            buy(ss, sc, itemNamed("FAMAS"), h);
            check(!sc.shield && sc.primary == itemNamed("FAMAS") && speedFor(kWeaponMp5, sc, false) == 240,
                  "a primary bought: the shield let go of; the FAMAS's 240");
            check(speedFor(kWeaponCrowbar, sc, false) == 250 && speedFor(kWeaponMp5, sc, true) == 250,
                  "the knife's and the bomb's 250");
            // The M4A1's silencer: off as bought, and its clips the
            // silenced ones once on.
            buy(ss, sc, itemNamed("M4A1"), h);
            check(silencerOf(kWeaponMp5, sc) == &sc.m4Silenced && !sc.m4Silenced &&
                      std::strcmp(heldClip("v_m4a1", "shoot1_unsil", sc), "shoot1_unsil") == 0,
                  "the M4A1 bought without its silencer");
            sc.m4Silenced = true;
            check(silencedInHand(kWeaponMp5, sc) && std::strcmp(heldClip("v_m4a1", "idle_unsil", sc), "idle") == 0 &&
                      std::strcmp(heldClip("v_m4a1", "reload_unsil", sc), "reload") == 0 &&
                      std::strcmp(heldClip("v_usp", "idle_unsil", sc), "idle_unsil") == 0,
                  "the silencer on: the M4A1's own silenced clips");
            buy(ss, sc, itemNamed("AWP"), h);
            check(speedFor(kWeaponCrossbow, sc, false, false) == 210 && speedFor(kWeaponCrossbow, sc, false, true) == 150,
                  "the AWP: 210, 150 scoped");
            // A round weaker for how far it goes: RANGE_MODIFER ^ (distance / 500).
            const CsGun &ak = *gunOfItem(itemNamed("AK-47")), &glock = *gunOfItem(itemNamed("GLOCK 18"));
            const CsGun &m3 = *gunOfItem(itemNamed("M3"));
            std::printf("  falloff: AK 36 at 0, 1000, 2000: %d %d %d; Glock 25 at 500, 750: %d %d\n", damageAfter(ak, 36, 0),
                        damageAfter(ak, 36, 1000), damageAfter(ak, 36, 2000), damageAfter(glock, 25, 500),
                        damageAfter(glock, 25, 750));
            check(damageAfter(ak, 36, 0) == 36 && damageAfter(ak, 36, 1000) == 35 && damageAfter(ak, 36, 2000) == 33 &&
                      damageAfter(glock, 25, 500) == 19 && damageAfter(glock, 25, 750) == 16 && damageAfter(m3, 20, 1500) == 20,
                  "damage falls off as 1.6's: the AK hardly, the Glock fast, the pump guns' pellets not");
        }
        Carry c;
        carryOut(c, state, arsenal, cs);
        PlayerState next;
        Arsenal nextArsenal;
        next.arsenal = &nextArsenal;
        PlayerCs nextCs = cs;
        newRoundLoadout(next, nextCs, c);
        check(hasWeapon(next, kWeaponMp5) && nextCs.primary == itemNamed("M4A1") && nextCs.money == cs.money,
              "lived: the M4 kept, and the money");
        check(nextCs.nvg, "lived: the night vision kept");
        state.health = 0;
        carryOut(c, state, arsenal, cs);
        PlayerState dead;
        Arsenal deadArsenal;
        dead.arsenal = &deadArsenal;
        PlayerCs deadCs = cs;
        newRoundLoadout(dead, deadCs, c);
        check(!hasWeapon(dead, kWeaponMp5) && hasWeapon(dead, kWeaponGlock) && deadCs.money == cs.money &&
                  deadCs.team == kTeamCT && dead.health == 100,
              "died: a pistol again, the money kept");
        check(!deadCs.nvg, "died: the night vision lost");
        char a[40], b[40];
        Round r;
        roundText(r, kTeamCT, a, sizeof(a), b, sizeof(b));
        check(std::strncmp(a, "BUY TIME", 8) == 0 && std::strcmp(b, "CT 0  -  T 0") == 0, "the touch screen's lines");
    }
    void testViews()
    {
        // Every weapon sold has a view model of its own in the table.
        for (int i = 0; i < kItemCount; ++i)
        {
            if (kItems[i].gear != kGearNone && kItems[i].gear != kGearHe)
                continue;
            check(kItems[i].view != nullptr && viewNamed(kItems[i].view) != nullptr, "each weapon its own view model");
        }
        PlayerCs cs;
        cs.team = kTeamCT;
        cs.pistol = 1;
        check(std::strcmp(viewModelFor(kWeaponCrowbar, cs, false), "v_knife") == 0, "the crowbar is the knife");
        check(std::strcmp(viewModelFor(kWeaponGlock, cs, false), "v_usp") == 0, "a CT's pistol: the USP");
        cs.primary = itemNamed("AK-47");
        check(std::strcmp(viewModelFor(kWeaponMp5, cs, false), "v_ak47") == 0, "an AK bought is held as one");
        cs.primary = itemNamed("AWP");
        check(std::strcmp(viewModelFor(kWeaponCrossbow, cs, false), "v_awp") == 0, "an AWP as one");
        check(std::strcmp(viewModelFor(kWeaponMp5, cs, true), "v_c4") == 0, "the bomb in hand");

        const CsView &ak = *viewNamed("v_ak47");
        check(std::strcmp(viewClip(ak, "shoot"), "shoot1") == 0 && std::strcmp(viewClip(ak, "longidle"), "idle1") == 0 &&
                  std::strcmp(viewClip(ak, "deploy"), "draw") == 0 && std::strcmp(viewClip(ak, "reload"), "reload") == 0,
              "the MP5's clips as the AK's");
        const CsView &m3 = *viewNamed("v_m3");
        check(std::strcmp(viewClip(m3, "start_reload"), "start_reload") == 0 &&
                  std::strcmp(viewClip(m3, "reload"), "insert") == 0 &&
                  std::strcmp(viewClip(m3, "pump"), "after_reload") == 0 &&
                  std::strcmp(viewClip(m3, "shoot_big"), "shoot1") == 0,
              "the shotgun's shell-by-shell reload as the M3's");
        const CsView &usp = *viewNamed("v_usp");
        check(std::strcmp(viewClip(usp, "shoot_empty"), "shootlast_unsil") == 0 &&
                  std::strcmp(viewClip(usp, "reload_noshot"), "reload_unsil") == 0,
              "the USP unsilenced, its last round");
        const CsView &knife = *viewNamed("v_knife");
        check(std::strcmp(viewClip(knife, "attack1miss"), "slash1") == 0 &&
                  std::strcmp(viewClip(knife, "attack2hit"), "slash2") == 0 &&
                  std::strcmp(viewClip(knife, "attack3"), "midslash1") == 0,
              "a crowbar's three swings the knife's three slashes");
        const CsView &he = *viewNamed("v_hegrenade");
        check(std::strcmp(viewClip(he, "pinpull"), "pullpin") == 0 && std::strcmp(viewClip(he, "throw2"), "throw") == 0,
              "the grenade's pin and throw");
        check(beepFor(kC4Ms) == CsSound::Beep1 && beepFor(kC4Ms / 2) == CsSound::Beep3 && beepFor(500) == CsSound::Beep5 &&
                  beepFor(-10) == CsSound::Beep5,
              "the bomb's beeps: the first, then on to the fifth as it runs down");
        char stem[64];
        check(soundCardStem(kCsSoundPaths[0], stem, sizeof(stem)) && std::strlen(stem) > 0, "its sounds have stems");
        const char *clips[12];
        check(viewClips(m3, clips, 12) == 6, "the M3's clips for the converter (start_reload with them)");

        // A console sent the game in DS mode: every gun that is neither the
        // map's own nor one of the guests' few held as one of those.
        bool covered = true;
        for (const CsView &v : kViews)
        {
            // (Not the shield's: a guest in DS mode holds the plain ones.)
            if (isShieldView(v.model))
                continue;
            bool few = viewAlwaysInMap(v.model);
            for (const char *g : kGuestViews)
                few = few || std::strcmp(g, v.model) == 0;
            int standIns = 0;
            for (const auto &one : kGuestStandIns)
            {
                if (std::strcmp(one.view, v.model) != 0)
                    continue;
                ++standIns;
                bool known = false;
                for (const char *g : kGuestViews)
                    known = known || std::strcmp(g, one.as) == 0;
                covered = covered && known && viewNamed(one.as) != nullptr;
            }
            covered = covered && (few ? standIns == 0 : standIns == 1);
        }
        check(covered, "the guests' guns: each other one held as one of them");
        cs.primary = itemNamed("GALIL");
        guestViews = true;
        check(std::strcmp(viewModelFor(kWeaponMp5, cs, false), "v_ak47") == 0, "a guest's Galil held as the AK");
        cs.primary = itemNamed("AK-47");
        check(std::strcmp(viewModelFor(kWeaponMp5, cs, false), "v_ak47") == 0, "... its AK as itself");
        guestViews = false;
        cs.primary = itemNamed("GALIL");
        check(std::strcmp(viewModelFor(kWeaponMp5, cs, false), "v_galil") == 0, "a card's Galil as itself");
    }
    /// Counter-Strike between two consoles (HlCsNet.hpp): the host a
    /// terrorist (seat 0), the client a counter-terrorist (seat 1). The
    /// host's round is the client's; a plant and a defuse said; who won
    /// paid on both; the bomb given; a kill paid.
    void testNetRounds()
    {
        auto hostMatch = std::make_unique<NetMatch>();
        auto clientMatch = std::make_unique<NetMatch>();
        NetMatch &h = *hostMatch, &c = *clientMatch;
        h.role = NetMatch::Role::Host;
        h.me = 0;
        c.role = NetMatch::Role::Client;
        c.me = 1;
        h.playing = c.playing = true;
        h.rounds = c.rounds = true;
        const std::uint8_t in = kNetHere | kNetInGame | kNetAlive;
        h.host.players[1].flags = in;
        c.client.players[0].flags = in;
        Setup hs, cs2;
        int hStart = -1, cStart = -1;
        netBegin(h, hs, hStart);
        const NetRounds hostRounds = netRounds; // (one console's each: the test keeps both)
        netBegin(c, cs2, cStart);
        NetRounds clientRounds = netRounds;
        check(hs.team == kTeamT && cs2.team == kTeamCT && hStart == 0 && cStart == 0,
              "no teams: the even seats terrorists, the odd counter-terrorists, each at their side's first start");
        check(netTeammates(h, 0, 1) == false && netTeammates(h, 0, 2) == true, "sides by seat: who may not be hurt");
        Round hr, cr;
        newGame(hr, hs, 0, true);
        newGame(cr, cs2, 0, true);
        PlayerCs hp, cp;
        hp.team = kTeamT;
        cp.team = kTeamCT;
        hp.money = cp.money = 800;
        NetRounds hostR = hostRounds, clientR = clientRounds;
        // Each frame: the host's round, then the client's -- what each said
        // carried to the other.
        auto carry = [](NetOutbox &from, NetMatch &to)
        {
            for (int i = 0; i < from.count; ++i)
            {
                const NetEvent &e = from.events[(from.head + i) % NetOutbox::kMost];
                if ((e.type == NetEventType::CsRound || e.type == NetEventType::CsPlant ||
                     e.type == NetEventType::CsDefuse || e.type == NetEventType::CsBomb ||
                     e.type == NetEventType::CsTake || e.type == NetEventType::CsDrop ||
                     e.type == NetEventType::CsOrder) &&
                    to.roundEventCount < NetMatch::kMostNotices)
                    to.roundEvents[to.roundEventCount++] = e;
            }
            from.head = (from.head + from.count) % NetOutbox::kMost;
            from.first = static_cast<std::uint16_t>(from.first + from.count);
            from.count = 0;
        };
        fx hostAt[3] = {fxFromInt(100), fxFromInt(200), fxFromInt(36)};
        fx clientAt[3] = {fxFromInt(900), fxFromInt(900), fxFromInt(36)};
        bool hostPlants = false, clientDefuses = false, clientAlive = true;
        RoundOut hSeen, cSeen;
        auto frame = [&](int ms)
        {
            RoundIn hi;
            hi.ms = ms;
            hi.team = kTeamT;
            hi.origin = hostAt;
            hi.inBombSite = hostPlants;
            hi.actionHeld = hostPlants;
            netRounds = hostR;
            const RoundOut ho = netRoundTick(h, hr, hi, hp);
            hostR = netRounds;
            carry(h.host.out, c);
            RoundIn ci;
            ci.ms = ms;
            ci.team = kTeamCT;
            ci.alive = clientAlive;
            ci.origin = clientAt;
            ci.actionHeld = clientDefuses;
            netRounds = clientR;
            const RoundOut co = netRoundTick(c, cr, ci, cp);
            clientR = netRounds;
            carry(c.client.out, h);
            for (RoundOut *seen : {&hSeen, &cSeen})
            {
                const RoundOut &o = seen == &hSeen ? ho : co;
                seen->started |= o.started;
                seen->unfrozen |= o.unfrozen;
                seen->planted |= o.planted;
                seen->exploded |= o.exploded;
                seen->defused |= o.defused;
                seen->ended |= o.ended;
            }
        };
        for (int t = 0; t < kFreezeMs + 500; t += 50)
            frame(50);
        check(hr.phase == Phase::Live && cr.phase == Phase::Live && cSeen.unfrozen, "the freeze over on both");
        check(hr.hasBomb && !cr.hasBomb && clientR.carrier == 0, "the bomb the host's: its only terrorist");
        check(cr.clockMs > hr.clockMs - 1500 && cr.clockMs < hr.clockMs + 1500, "the client's clock the host's");
        // The host plants; the client hears where.
        hostPlants = true;
        for (int t = 0; t < kPlantMs + 500; t += 50)
            frame(50);
        hostPlants = false;
        check(hr.phase == Phase::Planted && cr.phase == Phase::Planted && cSeen.planted, "planted, on both");
        check(cr.bomb[0] == hostAt[0] && cr.bomb[1] == hostAt[1], "the client knows where");
        // The client defuses by it: the host ends the round.
        for (int a = 0; a < 3; ++a)
            clientAt[a] = hostAt[a];
        clientDefuses = true;
        for (int t = 0; t < kDefuseMs + 1000 && hr.phase != Phase::Over; t += 50)
            frame(50);
        clientDefuses = false;
        frame(50);
        check(hr.phase == Phase::Over && hr.why == Why::BombDefused && cr.phase == Phase::Over && cSeen.defused,
              "defused by the client: the round the counter-terrorists', on both");
        check(cp.money == 800 + kWinObjective && hp.money == 800 + lossBonus(1) + kPlantedLossBonus,
              "each paid as the round has it");
        check(cr.scoreCT == 1 && hr.scoreCT == 1, "the score on both");
        // The next round: everyone back.
        cSeen = RoundOut();
        for (int t = 0; t < kRoundOverMs + 500; t += 50)
            frame(50);
        check(hr.number == 1 && cr.number == 1 && cSeen.started && cr.phase == Phase::Freeze, "the next round on both");
        for (int t = 0; t < kFreezeMs + 500; t += 50)
            frame(50);
        // The host puts the bomb down (the radio's DROP BOMB): it lies there,
        // on both; a counter-terrorist walking over it takes nothing; the
        // host's player (or a bot of its: botsThink) takes it up again.
        check(hr.hasBomb && hostR.carrier == 0, "the bomb the host's again");
        netRounds = hostR;
        const fx hostFeet[3] = {hostAt[0], hostAt[1], hostAt[2] - fxFromInt(36)};
        netDropBomb(h, hr, hostFeet);
        hostR = netRounds;
        frame(50);
        frame(50);
        check(hr.bombDropped && !hr.hasBomb && hostR.carrier < 0, "put down: nobody's, lying");
        check(cr.bombDropped && cr.bombDrop[0] == hostFeet[0] && cr.bombDrop[1] == hostFeet[1],
              "the client sees where it lies");
        NetEvent take = netEvent(NetEventType::CsTake);
        take.from = 1;
        h.roundEvents[h.roundEventCount++] = take;
        frame(50);
        check(hr.bombDropped && hostR.carrier < 0, "a counter-terrorist over it takes nothing");
        hr.hasBomb = true; // (walked over: botsThink's)
        hr.bombDropped = false;
        frame(50);
        frame(50);
        check(hostR.carrier == 0 && hr.hasBomb && !cr.bombDropped, "taken up again: the host's, lying nowhere");
        // An order from the client: the host's bots on the client's side take it.
        NetEvent order = netEvent(NetEventType::CsOrder, static_cast<int>(Order::GoB));
        order.from = 1;
        h.roundEvents[h.roundEventCount++] = order;
        const int ordersWere = teamOrders[1];
        frame(50);
        check(teamOrders[1] == ordersWere + 1 && teamOrder[1] == Order::GoB, "a client's order: the host's bots on its side");
        giveOrder(kTeamCT, Order::Auto);
        // The client shot down: the terrorists' round.
        clientAlive = false;
        h.host.players[1].flags = kNetHere | kNetInGame;
        for (int t = 0; t < 1000; t += 50)
            frame(50);
        check(hr.phase == Phase::Over && hr.why == Why::CtsDead && cr.phase == Phase::Over && cr.winner == kTeamT,
              "the counter-terrorists all down: the terrorists' round, on both");
        // A kill: paid.
        const int before = cp.money;
        c.kills[1] = 1;
        frame(50);
        check(cp.money == before + kNetKillReward, "a kill paid");
        netRounds = NetRounds();
    }
} // namespace

int main()
{
    testSetup();
    testCatalogue();
    testBuying();
    testZones();
    testPanel();
    testRounds();
    testCarry();
    testViews();
    testNetRounds();
    {
        int e = 0, m = 0;
        Setup b;
        b.tBots = 0;
        b.ctBots = 0;
        botSides(b, kTeamCT, e, m);
        check(e == 0 && m == 0, "no bots");
        b.tBots = 1;
        botSides(b, kTeamCT, e, m);
        check(e == 1 && m == 0, "one terrorist: against a counter-terrorist player");
        b.ctBots = 4;
        botSides(b, kTeamT, e, m);
        check(e == 4 && m == 1, "a terrorist player: the counter-terrorists against, the terrorist with");
        b.tBots = 5;
        b.ctBots = 0;
        botSides(b, kTeamCT, e, m);
        check(e == 5 && m == 0, "five against, none with");
        char name[24];
        botMakerName(name, sizeof(name), kTeamCT, true, 3);
        check(std::strcmp(name, "csbot_ct_s_3") == 0, "a maker's name");
    }
    if (g_failures == 0)
        std::printf("test_cs: ok\n");
    return g_failures == 0 ? 0 : 1;
}
