#define CSBOTS_DEBUG_PRINT 1
#include <cstdio>
#include <fstream>
#include <iterator>
// Counter-Strike's bots on a real map (HlCsBots.hpp): de_dust2 cooked as the
// converter cooks it, its bots woken as the game wakes them, and the game's
// own monster code run for a while -- do they all come, do they move, do
// they get where the round wants them?
//
// Needs Counter-Strike's files: HLDS_CSTRIKE_DIR, a folder with its maps/
// and models/ (and Half-Life's models beside them). Skipped without it.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

#include "HlCsBots.hpp"
#include "HlCsNet.hpp"
#include "HlCsRound.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    int whole(fx v) { return v >> kFxShift; }

    double apart(const fx *a, const fx *b)
    {
        const double dx = whole(a[0] - b[0]), dy = whole(a[1] - b[1]);
        return std::sqrt(dx * dx + dy * dy);
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_CSTRIKE_DIR");
    if (dir == nullptr || *dir == '\0')
    {
        std::printf("test_csbots: skipped (no HLDS_CSTRIKE_DIR)\n");
        return 77;
    }
    activeGame = kGameCStrike;
    // (Another map than dust2: CSBOTS_MAP.)
    const std::string mapName = std::getenv("CSBOTS_MAP") != nullptr ? std::getenv("CSBOTS_MAP") : "de_dust2";
    const bool dust2 = mapName == "de_dust2";
    auto game = std::make_unique<Game>();
    Game &g = *game;
    std::string error;
    if (!g.bsp.load(std::string(dir) + "/maps/" + mapName + ".bsp", error))
    {
        std::printf("FAIL: %s does not load: %s\n", mapName.c_str(), error.c_str());
        return 1;
    }
    // (Its bots' navigation, if it is there beside it: the nodes from that.)
    if (std::getenv("CSBOTS_NONAV") == nullptr)
    {
        std::ifstream file(std::string(dir) + "/maps/" + mapName + ".nav", std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string why;
        if (!bytes.empty() && parseNavMesh(bytes, g.navMesh, why))
            std::printf("%s.nav: %zu areas, %zu places\n", mapName.c_str(), g.navMesh.areas.size(), g.navMesh.places.size());
    }
    finish(g, nullptr, dir);
    check(g.ok, "the map cooks");
    if (!g.ok)
        return 1;
    std::printf("path nodes %d, links %d\n", g.actors.nodeCount, g.actors.linkCount);
    // (The nodes and their links, for a look at the map: CSBOTS_DUMP=file.)
    if (const char *dump = std::getenv("CSBOTS_DUMP"))
    {
        if (std::FILE *out = std::fopen(dump, "w"))
        {
            for (int v = 0; v < g.actors.nodeCount; ++v)
            {
                const PathNode &nd = g.actors.nodes[v];
                const int place = pathNodePlace(nd);
                std::fprintf(out, "N %d %d %d %d %s", v, whole(nd.pos[0]), whole(nd.pos[1]), whole(nd.pos[2]),
                             place > 0 && place <= static_cast<int>(g.navMesh.places.size()) ? g.navMesh.places[place - 1].c_str() : "-");
                for (int l = nd.firstLink; l < nd.firstLink + nd.linkCount; ++l)
                    std::fprintf(out, " %d:%x%s", g.actors.links[l].to, g.actors.links[l].flags, g.actors.links[l].brush != kNoBrush ? "D" : "");
                std::fprintf(out, "\n");
            }
            for (const auto &area : g.navMesh.areas)
            {
                float mid[3];
                area.middle(mid);
                std::fprintf(out, "A %d %d %d %d %d %d %d %u %s\n", static_cast<int>(mid[0]), static_cast<int>(mid[1]),
                             static_cast<int>(mid[2]), static_cast<int>(area.lo[0]), static_cast<int>(area.lo[1]),
                             static_cast<int>(area.hi[0]), static_cast<int>(area.hi[1]), area.flags,
                             area.place > 0 ? g.navMesh.places[area.place - 1].c_str() : "-");
            }
            std::fclose(out);
        }
    }
    check(g.actors.nodeCount > 100, "nodes laid over the map's floors");
    {
        int count[32] = {};
        for (int v = 0; v < g.actors.nodeCount; ++v)
            ++count[pathNodePlace(g.actors.nodes[v])];
        for (int p = 0; p < 32; ++p)
            if (count[p] > 0)
                std::printf("  place %s: %d nodes\n",
                            p > 0 && p <= static_cast<int>(g.navMesh.places.size()) ? g.navMesh.places[p - 1].c_str() : "-",
                            count[p]);
    }
    // (Room for the bots to think: as the game makes it.)
    {
        const int thinkers = aiNeeded(g.actors) + 2 * cs::kBotSlotsPerSide;
        g.aiMonsters.assign(thinkers, AiMonster());
        g.aiSlots.assign(g.actors.count > 0 ? g.actors.count : 1, -1);
        g.aiHeld.assign(g.actors.count > 0 ? g.actors.count : 1, 0);
        aiAttach(g.actors, g.ai, g.aiMonsters.data(), thinkers, g.aiSlots.data(), g.aiHeld.data(), g.actors.count);
    }

    // The player, a counter-terrorist at their first start, out of the
    // way (no target: the round is the bots' to play).
    const EntityRecord *start = nullptr;
    check(cs::teamStart(g.view, cs::kTeamCT, 0, start), "a counter-terrorist start");
    for (int a = 0; a < 3; ++a)
        g.player.origin[a] = start->origin[a];
    // (Below the map: in nobody's way.)
    g.player.origin[2] -= fxFromInt(8000);
    static Cheats cheats;
    cheats.notarget = true;
    cheats.god = true;
    g.player.cheats = &cheats;

    // The ways: from the terrorists' start to each site, and to the
    // other side's start.
    {
        const EntityRecord *tStart = nullptr;
        check(cs::teamStart(g.view, cs::kTeamT, 0, tStart), "a terrorist start");
        const fx tFeet[3] = {tStart->origin[0], tStart->origin[1], tStart->origin[2] - fxFromInt(36)};
        const int from = pathNearest(g.actors, *g.logic.world, tFeet);
        // (Every start of each side: a way from it to each site.)
        for (int team = 0; team < 2; ++team)
            for (int k = 0; k < 10; ++k)
            {
                const EntityRecord *st = nullptr;
                if (!cs::teamStart(g.view, team == 0 ? cs::kTeamT : cs::kTeamCT, k, st))
                    break;
                const fx feet[3] = {st->origin[0], st->origin[1], st->origin[2] - fxFromInt(36)};
                const int f = pathNearest(g.actors, *g.logic.world, feet);
                int ways = 0;
                for (int s = 0; s < 2; ++s)
                {
                    const fx *site = s == 0 ? g.actors.nodes[0].pos : g.actors.nodes[g.actors.nodeCount - 1].pos;
                    (void)site;
                }
                std::printf("  start %s %d at %d %d %d: node %d (at %d %d %d)\n", team == 0 ? "T" : "CT", k,
                            whole(feet[0]), whole(feet[1]), whole(feet[2]), f, f >= 0 ? whole(g.actors.nodes[f].pos[0]) : 0,
                            f >= 0 ? whole(g.actors.nodes[f].pos[1]) : 0, f >= 0 ? whole(g.actors.nodes[f].pos[2]) : 0);
                (void)ways;
                if (f >= 0)
                {
                    const PathNode &nd = g.actors.nodes[f];
                    std::printf("    links %d:", nd.linkCount);
                    for (int l = nd.firstLink; l < nd.firstLink + nd.linkCount; ++l)
                        std::printf(" ->%d(%x)", g.actors.links[l].to, g.actors.links[l].flags);
                    std::printf("; to 55: %d, to 75: %d\n", pathStep(g.actors, f, 55, kPathLinkHuman, false),
                                pathStep(g.actors, f, 75, kPathLinkHuman, false));
                }
            }
        auto reach = [&](const char *what, const fx *to)
        {
            const int n = pathNearest(g.actors, *g.logic.world, to);
            const bool way = n >= 0 && from >= 0 && (n == from || pathStep(g.actors, from, n, kPathLinkHuman, false) != kNoPathNode);
            std::printf("  %s at %d %d %d: node %d, a way %d\n", what, whole(to[0]), whole(to[1]), whole(to[2]), n, way);
            check(way, what);
        };
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::BombTarget))
                continue;
            const fx mid[3] = {fxFromInt((r.mins[0] + r.maxs[0]) / 2), fxFromInt((r.mins[1] + r.maxs[1]) / 2),
                               fxFromInt(r.mins[2] + 1)};
            reach("a bomb site", mid);
        }
        const fx feet[3] = {start->origin[0], start->origin[1], start->origin[2] - fxFromInt(36)};
        reach("the CT start", feet);
    }

    cs::Setup setup;
    setup.tBots = 3;
    setup.ctBots = 2;
    setup.team = cs::kTeamCT;
    cs::BotRoster roster;
    const cs::BotWake wake = cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamCT, setup, roster);
    std::printf("makers %d, woken %d, no place %d; enemies %d, mates %d\n", wake.makers, wake.woken, wake.noPlace,
                roster.count[0], roster.count[1]);
    check(wake.makers == 20, "twenty makers: a soldier's and a guard's at five starts a side");
    check(roster.count[0] == 3 && roster.count[1] == 2, "five bots: three against, two with the player");

    hldata::csBots = true;
    hldata::csShotDamage = &cs::botShotDamage;
    hldata::csTakeHook = &cs::botTakes;
    hldata::muteSentences = true;

    cs::Round round;
    cs::PlayerCs pcs;
    pcs.team = cs::kTeamCT;
    round.bombMap = true;
    round.phase = cs::Phase::Live;
    round.clockMs = round.roundMs;
    cs::botsNewRound(g.view, g.actors, roster, cs::kTeamCT, round, cs::setup);

    for (int w = 0; w < 2; ++w)
        for (int n = 0; n < roster.count[w]; ++n)
            std::printf("  roster %d %d: actor %d\n", w, n, roster.slot[w][n]);
    std::printf("sites %d (spots %d, %d); the bomb with actor %d; to site %d\n", cs::brain.map.sites,
                cs::brain.map.spots[0], cs::brain.map.spots[1], round.botBomb, cs::brain.site);
    check(round.botBomb == roster.slot[0][0], "a terrorist bot has the bomb");
    std::printf("  the play %d to site %d (a fake at %d); stages %d %d\n", static_cast<int>(cs::tPlan.play), cs::tPlan.site,
                cs::tPlan.fake, cs::brain.stageOk[0], cs::brain.stageOk[1]);

    fx was[2][cs::kBotSlotsPerSide][3];
    for (int w = 0; w < 2; ++w)
        for (int n = 0; n < roster.count[w]; ++n)
            for (int a = 0; a < 3; ++a)
                was[w][n][a] = g.actors.states[roster.slot[w][n]].pos[a];

    // Up to a round's time of the game.
    int plantedAt = -1, defusedAt = -1;
    bool everDefusing = false;
    int firstCarrier = -1;
    bool dropSeen = false;
    for (int t = 0; t < 150000 && defusedAt < 0; t += 33)
    {
        // (At 20 s, its carrier shot down: the bomb is to be dropped, and
        // fetched by another.)
        if (t == 19998 && round.botBomb >= 0 && round.phase == cs::Phase::Live)
        {
            firstCarrier = round.botBomb;
            g.actors.states[firstCarrier].health = 0;
            g.actors.states[firstCarrier].ai = kAiDead;
        }
        dropSeen = dropSeen || round.bombDropped;
        logicTick(g.logic, g.player, 33);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, 33);
        if (round.phase == cs::Phase::Live)
        {
            round.liveMs += 33;
            round.clockMs = round.clockMs > 33 ? round.clockMs - 33 : 0;
        }
        const cs::BotOut out = cs::botsThink(g.logic, g.player, g.view, roster, cs::kTeamCT, round, pcs, 33);
        if (round.phase == cs::Phase::Planted)
            round.bombMs = 60000; // (no blast in the test: time to defuse)
        everDefusing = everDefusing || out.defusing;
        if (out.planted)
        {
            plantedAt = t;
            std::printf("  planted at %d ms, at %d %d %d\n", t, whole(round.bomb[0]), whole(round.bomb[1]), whole(round.bomb[2]));
        }
        if (out.defused)
        {
            defusedAt = t;
            std::printf("  defused at %d ms\n", t);
        }
        g.drain();
        if (std::getenv("CSBOTS_MIND") != nullptr && t > 95000 && t < 110000 && t % 1000 < 33)
        {
            const cs::BotMind &mm = cs::brain.mind[0][1];
            const ActorState &bb = g.actors.states[roster.slot[0][1]];
            std::printf("  mind T1: pos %d,%d,%d face %d heard %d react %d still %d stuck %d detour %d tries %d goal %d,%d play %d com %d\n",
                        whole(bb.pos[0]), whole(bb.pos[1]), whole(bb.pos[2]), mm.faceMs, mm.heardMs, mm.reactMs, mm.stillMs,
                        mm.stuckMs, mm.detourMs, mm.tries, whole(mm.goal[0]), whole(mm.goal[1]),
                        static_cast<int>(cs::tPlan.play), cs::tPlan.committed);
        }
        if (t % 5000 < 33)
        {
            std::printf("  t %ds:", t / 1000);
            for (int w = 0; w < 2; ++w)
                for (int n = 0; n < roster.count[w]; ++n)
                {
                    const ActorState &bot = g.actors.states[roster.slot[w][n]];
                    const AiMonster *m = aiOf(g.actors, roster.slot[w][n]);
                    std::printf(" [%c%d %d,%d h%d ai%d %s e%d st%d g%d,%d]", w == 0 ? 'T' : 'C', n, whole(bot.pos[0]), whole(bot.pos[1]), static_cast<int>(bot.health), bot.ai,
                                m && m->schedule ? m->schedule->name : "-", m ? m->enemy : -9, m ? m->state : -9,
                                whole(cs::brain.mind[w][n].goal[0]), whole(cs::brain.mind[w][n].goal[1]));
                }
            std::printf("\n");
        }
    }
    for (int w = 0; w < 2; ++w)
    {
        for (int n = 0; n < roster.count[w]; ++n)
        {
            const ActorState &bot = g.actors.states[roster.slot[w][n]];
            std::printf("  %s %d: now %d %d %d, moved %.0f, active %d ai %d health %d\n", w == 0 ? "T" : "CT", n,
                        whole(bot.pos[0]), whole(bot.pos[1]), whole(bot.pos[2]), apart(was[w][n], bot.pos), bot.active,
                        bot.ai, static_cast<int>(bot.health));
        }
    }
    std::printf("  the bomb at the end: actor %d, phase %d\n", round.botBomb, static_cast<int>(round.phase));
    check(firstCarrier < 0 || (dropSeen && (round.botBomb != firstCarrier || round.plantedThisRound)),
          "the bomb, its carrier down, is dropped and picked up by another");
    for (int w = 0; w < 2; ++w)
        for (int n = 0; n < roster.count[w]; ++n)
            check(apart(was[w][n], g.actors.states[roster.slot[w][n]].pos) > 300 ||
                      (w == 1 && round.phase != cs::Phase::Live && round.phase != cs::Phase::Planted) ||
                      apart(was[w][n], g.actors.states[roster.slot[w][n]].pos) > 100,
                  "every bot sets out");
    check(plantedAt >= 0 || cs::botsAlive(g.actors, roster, 0) == 0 || cs::botsAlive(g.actors, roster, 1) == 0,
          "the round comes to something: the bomb down, or a side out");
    (void)everDefusing;
    (void)defusedAt;

    // A lone terrorist bot with the bomb: it goes to a site and plants it.
    {
        cs::Setup one;
        one.tBots = 1;
        one.ctBots = 0;
        one.team = cs::kTeamCT;
        cs::BotRoster solo;
        cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamCT, one, solo);
        check(solo.count[0] == 1 && solo.count[1] == 0, "one bot: a terrorist");
        cs::Round r1;
        cs::PlayerCs p1;
        p1.team = cs::kTeamCT;
        r1.bombMap = true;
        r1.number = 2;
        r1.phase = cs::Phase::Live;
        cs::botsNewRound(g.view, g.actors, solo, cs::kTeamCT, r1, cs::setup);
        int planted = -1;
        for (int t = 0; t < 120000 && planted < 0; t += 33)
        {
            logicTick(g.logic, g.player, 33);
            g.logic.viewTurn = 0;
            monstersTick(g.logic, g.player, 33);
            if (cs::botsThink(g.logic, g.player, g.view, solo, cs::kTeamCT, r1, p1, 33).planted)
                planted = t;
            if (std::getenv("CSBOTS_LONE") != nullptr && t % 2000 < 33)
            {
                const ActorState &b = g.actors.states[solo.slot[0][0]];
                const AiMonster *m = aiOf(g.actors, solo.slot[0][0]);
                const cs::BotMind &mind = cs::brain.mind[0][0];
                std::printf("    lone %ds %d,%d,%d %s goal %d,%d,%d node %d/%d hasGoal %d still %d detour %d\n", t / 1000, whole(b.pos[0]),
                            whole(b.pos[1]), whole(b.pos[2]), m && m->schedule ? m->schedule->name : "-", whole(mind.goal[0]),
                            whole(mind.goal[1]), whole(mind.goal[2]), b.fromNode, b.goalNode, b.hasGoal ? 1 : 0, mind.stillMs, mind.detourMs);
            }
            g.drain();
        }
        const ActorState &tb = g.actors.states[solo.slot[0][0]];
        std::printf("  a lone T: %d %d %d, to site %d; planted at %d ms\n", whole(tb.pos[0]), whole(tb.pos[1]),
                    whole(tb.pos[2]), cs::brain.site, planted);
        check(planted >= 0 && r1.phase == cs::Phase::Planted, "a terrorist bot plants the bomb");
    }

    // The other way about: the bomb down in a site, one counter-terrorist
    // bot left to defuse it (the player a terrorist, out of the way; their
    // mate out of it).
    {
        cs::Setup two;
        two.tBots = 1;
        two.ctBots = 1;
        two.team = cs::kTeamT;
        cs::BotRoster pair;
        cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamT, two, pair);
        check(pair.count[0] == 1 && pair.count[1] == 1, "two bots: a counter-terrorist and a terrorist");
        ActorState &mate = g.actors.states[pair.slot[1][0]];
        aiForget(g.actors, pair.slot[1][0]);
        mate.active = false;
        mate.ai = kAiDead;
        cs::Round r2;
        cs::PlayerCs p2;
        p2.team = cs::kTeamT;
        r2.bombMap = true;
        r2.number = 1;
        r2.phase = cs::Phase::Live;
        cs::botsNewRound(g.view, g.actors, pair, cs::kTeamT, r2, cs::setup);
        check(r2.botBomb < 0, "the player a terrorist: the bomb is theirs");
        const fx *spot = cs::brain.map.spot[0][0];
        r2.phase = cs::Phase::Planted;
        r2.bombMs = 60000;
        for (int a = 0; a < 3; ++a)
            r2.bomb[a] = spot[a];
        r2.bomb[2] += fxFromInt(36);
        int defused = -1;
        for (int t = 0; t < 120000 && defused < 0; t += 33)
        {
            logicTick(g.logic, g.player, 33);
            g.logic.viewTurn = 0;
            monstersTick(g.logic, g.player, 33);
            const cs::BotOut out = cs::botsThink(g.logic, g.player, g.view, pair, cs::kTeamT, r2, p2, 33);
            r2.bombMs = 60000;
            if (out.defused)
                defused = t;
            g.drain();
        }
        const ActorState &ct = g.actors.states[pair.slot[0][0]];
        std::printf("  a lone CT: %d %d %d; the bomb at %d %d; defused at %d ms\n", whole(ct.pos[0]), whole(ct.pos[1]),
                    whole(ct.pos[2]), whole(r2.bomb[0]), whole(r2.bomb[1]), defused);
        check(defused >= 0 && r2.phase == cs::Phase::Over && r2.winner == cs::kTeamCT, "a counter-terrorist bot defuses");
    }

    // A watch on bots against bots alone (CSBOTS_WATCH=<rounds>; the player
    // out of the map): where one walks about getting nowhere with no enemy
    // (a loop), and how long two of opposite sides in plain sight of each
    // other go before either takes the other on.
    if (const char *watch = std::getenv("CSBOTS_WATCH"))
    {
        const int roundsWanted = std::atoi(watch) > 0 ? std::atoi(watch) : 4;
        // (CSBOTS_T: the player a terrorist -- their side's bots the guards.)
        const std::uint8_t mine = std::getenv("CSBOTS_T") != nullptr ? cs::kTeamT : cs::kTeamCT;
        const EntityRecord *tStart = nullptr;
        const bool tStartFound = cs::teamStart(g.view, cs::kTeamT, 0, tStart);
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = mine == cs::kTeamT && tStartFound ? tStart->origin[a] : start->origin[a];
        // (CSBOTS_PLAYER: the player in it, at their start, standing.)
        const bool playerIn = std::getenv("CSBOTS_PLAYER") != nullptr;
        if (!playerIn)
            g.player.origin[2] -= fxFromInt(8000);
        static Cheats only;
        only.god = true;
        g.player.cheats = &only;
        cs::Setup full;
        full.tBots = 3;
        full.ctBots = 3;
        if (std::getenv("CSBOTS_FIVE") != nullptr)
            full.tBots = full.ctBots = 4;
        cs::BotRoster six;
        cs::Round rr;
        cs::PlayerCs pp;
        pp.team = mine;
        rr.bombMap = true;
        bool due = true;
        int rounds = 0;
        auto placeOf = [&](const fx *at) -> std::string
        {
            const int n = pathNearest(g.actors, *g.logic.world, at);
            const int place = n >= 0 ? pathNodePlace(g.actors.nodes[n]) : 0;
            return place > 0 && place <= static_cast<int>(g.navMesh.places.size()) ? g.navMesh.places[place - 1] : "-";
        };
        // (Each bot's place once a second, the last ten.)
        struct Trail
        {
            fx at[10][3];
            int count = 0;
            double walked = 0;
            fx last[3] = {0, 0, 0};
            bool lastSet = false;
            int quietMs = 0;
        };
        Trail trails[2][cs::kBotSlotsPerSide];
        int loops = 0;
        // (Since when each pair has been in sight of each other, unengaged.)
        int sightMs[cs::kBotSlotsPerSide][cs::kBotSlotsPerSide] = {};
        int waits = 0, slow = 0, stalemates = 0;
        long long waited = 0;
        int worstWait = 0;
        for (int t = 0; t < 150000 * roundsWanted && rounds < roundsWanted; t += 33)
        {
            if (due)
            {
                due = false;
                cs::wakeBots(g.view, g.logic, g.actors, mine, full, six);
                cs::botsNewRound(g.view, g.actors, six, mine, rr, cs::setup);
                for (auto &side : trails)
                    for (Trail &tr : side)
                        tr = Trail();
                for (auto &row : sightMs)
                    for (int &v : row)
                        v = 0;
            }
            logicTick(g.logic, g.player, 33);
            g.logic.viewTurn = 0;
            monstersTick(g.logic, g.player, 33);
            cs::botsThink(g.logic, g.player, g.view, six, mine, rr, pp, 33);
            cs::RoundIn in;
            in.ms = 33;
            in.alive = playerIn;
            in.team = mine;
            in.origin = g.player.origin;
            in.enemiesPlaying = six.count[0];
            in.enemiesAlive = cs::botsAlive(g.actors, six, 0);
            in.matesPlaying = six.count[1];
            in.matesAlive = cs::botsAlive(g.actors, six, 1);
            const cs::RoundOut ro = cs::roundTick(rr, in, pp);
            const bool live = rr.phase == cs::Phase::Live || rr.phase == cs::Phase::Planted;
            // (CSBOTS_WATCHBOT=<w><n>: one bot's going, the first 15 s of each round.)
            if (const char *one = std::getenv("CSBOTS_WATCHBOT"); one != nullptr && rr.phaseMs < (std::getenv("CSBOTS_WATCHMS") ? std::atoi(std::getenv("CSBOTS_WATCHMS")) : 15000) && t % 500 < 33)
            {
                const int w = one[0] == 'T' ? 0 : 1, n = one[1] - '0';
                const int index = six.slot[w][n];
                const ActorState &bot = g.actors.states[index];
                const AiMonster *m = aiOf(g.actors, index);
                const cs::BotMind &mind = cs::brain.mind[w][n];
                std::printf("    %s r%d ph%d %dms: %d,%d goal %d,%d (has %d) move %d,%d %s\n", one, rounds,
                            static_cast<int>(rr.phase), rr.phaseMs, whole(bot.pos[0]), whole(bot.pos[1]), whole(mind.goal[0]),
                            whole(mind.goal[1]), mind.hasGoal ? 1 : 0, m ? whole(m->moveGoal[0]) : 0,
                            m ? whole(m->moveGoal[1]) : 0, m && m->schedule ? m->schedule->name : "-");
            }
            // Fights given up (each in sight, nobody hurt a while): where, how far.
            for (int w = 0; w < 2 && live; ++w)
                for (int n = 0; n < six.count[w]; ++n)
                {
                    const cs::BotMind &mind = cs::brain.mind[w][n];
                    if (mind.pushMs != cs::kPushMs - 33)
                        continue;
                    const ActorState &bot = g.actors.states[six.slot[w][n]];
                    const AiMonster *m = aiOf(g.actors, six.slot[w][n]);
                    const int e = m != nullptr ? m->enemy : -1;
                    const double far = e >= 0 && e < g.actors.count ? apart(bot.pos, g.actors.states[e].pos) : -1;
                    ++stalemates;
                    std::printf("  STALEMATE r%d %ds %c%d (%s) with %d (%s) %.0f apart\n", rounds, t / 1000, w == 0 ? 'T' : 'C', n,
                                placeOf(bot.pos).c_str(), e, e >= 0 && e < g.actors.count ? placeOf(g.actors.states[e].pos).c_str() : "-",
                                far);
                }
            // Loops.
            for (int w = 0; w < 2 && live; ++w)
                for (int n = 0; n < six.count[w]; ++n)
                {
                    const ActorState &bot = g.actors.states[six.slot[w][n]];
                    const AiMonster *m = aiOf(g.actors, six.slot[w][n]);
                    Trail &tr = trails[w][n];
                    if (!cs::botUp(bot) || m == nullptr)
                        continue;
                    if (tr.lastSet)
                        tr.walked += apart(tr.last, bot.pos);
                    for (int k = 0; k < 3; ++k)
                        tr.last[k] = bot.pos[k];
                    tr.lastSet = true;
                    tr.quietMs = m->enemy == kAiNone ? tr.quietMs + 33 : 0;
                    if (t % 1000 >= 33)
                        continue;
                    if (tr.count == 10)
                    {
                        for (int i = 1; i < 10; ++i)
                            for (int k = 0; k < 3; ++k)
                                tr.at[i - 1][k] = tr.at[i][k];
                        tr.count = 9;
                    }
                    for (int k = 0; k < 3; ++k)
                        tr.at[tr.count][k] = bot.pos[k];
                    ++tr.count;
                    if (tr.count == 10)
                    {
                        double most = 0;
                        for (int i = 1; i < 10; ++i)
                            most = std::max(most, apart(tr.at[0], tr.at[i]));
                        if (tr.walked > 500 && most < 400)
                        {
                            ++loops;
                            std::printf("  LOOP r%d %ds %c%d at %d,%d (%s): walked %.0f in 10 s, never %.0f from the start; %s, enemy %d (%s)\n",
                                        rounds, t / 1000, w == 0 ? 'T' : 'C', n, whole(bot.pos[0]), whole(bot.pos[1]),
                                        placeOf(bot.pos).c_str(), tr.walked, most,
                                        m->schedule != nullptr ? m->schedule->name : "-", m->enemy,
                                        tr.quietMs > 10000 ? "none 10 s" : "lately");
                            for (int i = 0; i < 10; ++i)
                                std::printf("%s%d,%d", i ? " " : "    ", whole(tr.at[i][0]), whole(tr.at[i][1]));
                            std::printf("\n");
                            tr.count = 0;
                        }
                        tr.walked = 0;
                    }
                }
            // Sight.
            for (int i = 0; i < six.count[0] && live; ++i)
                for (int j = 0; j < six.count[1]; ++j)
                {
                    const ActorState &tb = g.actors.states[six.slot[0][i]];
                    const ActorState &cb = g.actors.states[six.slot[1][j]];
                    const AiMonster *tm = aiOf(g.actors, six.slot[0][i]);
                    const AiMonster *cm = aiOf(g.actors, six.slot[1][j]);
                    int &ms = sightMs[i][j];
                    if (!cs::botUp(tb) || !cs::botUp(cb) || tm == nullptr || cm == nullptr)
                    {
                        ms = 0;
                        continue;
                    }
                    const bool engaged = tm->enemy == six.slot[1][j] || cm->enemy == six.slot[0][i];
                    const double far = apart(tb.pos, cb.pos);
                    const fx te[3] = {tb.pos[0], tb.pos[1], tb.pos[2] + fxFromInt(60)};
                    const fx ce[3] = {cb.pos[0], cb.pos[1], cb.pos[2] + fxFromInt(60)};
                    const bool seen = far < 2000 && lineClear(*g.logic.world, te, ce);
                    if (seen && !engaged)
                        ms += 33;
                    else
                    {
                        if (ms > 0 && engaged)
                        {
                            ++waits;
                            waited += ms;
                            worstWait = std::max(worstWait, ms);
                            if (ms >= 1200)
                            {
                                ++slow;
                                std::printf("  SLOW r%d %ds T%d (%s, %s) and C%d (%s, %s) %.0f apart: %d ms in sight\n", rounds,
                                            t / 1000, i, placeOf(tb.pos).c_str(), tm->schedule ? tm->schedule->name : "-", j,
                                            placeOf(cb.pos).c_str(), cm->schedule ? cm->schedule->name : "-", far, ms);
                            }
                        }
                        ms = seen ? ms : 0;
                        if (engaged)
                            ms = 0;
                    }
                    if (ms == 3000)
                        std::printf("  UNSEEN r%d %ds T%d (%s, %s, yaw %d) and C%d (%s, %s, yaw %d) %.0f apart: 3 s in sight\n",
                                    rounds, t / 1000, i, placeOf(tb.pos).c_str(), tm->schedule ? tm->schedule->name : "-",
                                    tb.yaw * 360 / 65536, j, placeOf(cb.pos).c_str(), cm->schedule ? cm->schedule->name : "-",
                                    cb.yaw * 360 / 65536, far);
                }
            if (ro.started)
            {
                due = true;
                ++rounds;
                std::printf("  round %d over at %d s (why %d, planted %d)\n", rounds, t / 1000, static_cast<int>(rr.why),
                            rr.plantedThisRound ? 1 : 0);
            }
            g.drain();
        }
        std::printf("watch: rounds %d, loops %d, stalemates %d; engagements %d, %d slow (1.2 s+), mean %lld ms in sight first, worst %d ms\n",
                    rounds, loops, stalemates, waits, slow, waits ? waited / waits : 0, worstWait);
        return 0;
    }

    // As the game plays it: the player a counter-terrorist at their start,
    // in the fight (not hidden), three bots a side, the round's clock run
    // from the freeze through rounds -- nothing to stall a frame.
    {
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = start->origin[a];
        static Cheats only;
        only.god = true;
        g.player.cheats = &only;
        cs::Setup full;
        full.tBots = 3;
        full.ctBots = 3;
        cs::BotRoster six;
        cs::Round rr;
        cs::PlayerCs pp;
        pp.team = cs::kTeamCT;
        rr.bombMap = true;
        bool due = true;
        long long worst = 0;
        int rounds = 0;
        const int roundsWanted = std::getenv("CSBOTS_ROUNDS") != nullptr ? std::atoi(std::getenv("CSBOTS_ROUNDS")) : 3;
        std::uint32_t visited[cs::kBotSlotsPerSide] = {};
        int boughtRifles = 0, boughtGrenades = 0;
        cs::botGrenadesThrown = 0;
        hldata::csGrenadeHook = &cs::grenadeGoesOff;
        for (int t = 0; t < 140000 * roundsWanted && rounds < roundsWanted; t += 33)
        {
            // (Where each terrorist bot has been this round: the map's places.)
            for (int n = 0; n < six.count[0]; ++n)
            {
                const ActorState &bot = g.actors.states[six.slot[0][n]];
                if (bot.active && rr.phase == cs::Phase::Live)
                {
                    const int at = pathNearest(g.actors, *g.logic.world, bot.pos);
                    if (at >= 0)
                        visited[n] |= 1u << pathNodePlace(g.actors.nodes[at]);
                }
            }
            const auto before = std::chrono::steady_clock::now();
            if (due)
            {
                due = false;
                cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamCT, full, six);
                cs::botsNewRound(g.view, g.actors, six, cs::kTeamCT, rr, cs::setup);
                // (What they bought, frozen: a rifle, once they can.)
                for (int w = 0; w < 2; ++w)
                    for (int n = 0; n < six.count[w]; ++n)
                    {
                        boughtRifles += cs::brain.purse[w][n].gun == cs::kGunAk47 || cs::brain.purse[w][n].gun == cs::kGunM4a1;
                        boughtGrenades += cs::brain.purse[w][n].grenade != cs::kGearNone;
                    }
            }
            logicTick(g.logic, g.player, 33);
            g.logic.viewTurn = 0;
            monstersTick(g.logic, g.player, 33);
            const cs::BotOut bo = cs::botsThink(g.logic, g.player, g.view, six, cs::kTeamCT, rr, pp, 33);
            cs::RoundIn in;
            in.ms = 33;
            in.alive = g.player.health > 0;
            in.team = cs::kTeamCT;
            in.origin = g.player.origin;
            in.enemiesPlaying = six.count[0];
            in.enemiesAlive = cs::botsAlive(g.actors, six, 0);
            in.matesPlaying = six.count[1];
            in.matesAlive = cs::botsAlive(g.actors, six, 1);
            const cs::RoundOut ro = cs::roundTick(rr, in, pp);
            if (ro.started)
            {
                due = true;
                ++rounds;
                std::printf("  round over at %d s (%s; why %d, play %d, site %d, planted %d)\n", t / 1000,
                            bo.defused ? "defused" : "", static_cast<int>(rr.why), static_cast<int>(cs::tPlan.play),
                            cs::tPlan.site, rr.plantedThisRound ? 1 : 0);
                for (int n = 0; n < six.count[0]; ++n)
                {
                    std::printf("    T%d went by:", n);
                    for (int p = 1; p < 32; ++p)
                        if (visited[n] & (1u << p))
                            std::printf(" %s", p <= static_cast<int>(g.navMesh.places.size()) ? g.navMesh.places[p - 1].c_str() : "?");
                    std::printf("\n");
                    visited[n] = 0;
                }
            }
            if (std::getenv("CSBOTS_T0") != nullptr && t < 45000 && t % 1000 < 33)
            {
                const ActorState &bot = g.actors.states[six.slot[0][0]];
                const AiMonster *m = aiOf(g.actors, six.slot[0][0]);
                std::printf("  T0 %ds: %d,%d,%d %s #%d st%d e%d mg%d,%d yaw%d\n", t / 1000, whole(bot.pos[0]),
                            whole(bot.pos[1]), whole(bot.pos[2]), m && m->schedule ? m->schedule->name : "-",
                            m ? m->scheduleIndex : -1, m ? m->taskStatus : -1, m ? m->enemy : -9,
                            m ? whole(m->moveGoal[0]) : 0, m ? whole(m->moveGoal[1]) : 0, bot.yaw * 360 / 65536);
            }
            if (std::getenv("CSBOTS_TRACE") != nullptr && t % 10000 < 33)
            {
                std::printf("  r%d t %ds ph%d:", rounds, t / 1000, static_cast<int>(rr.phase));
                for (int w = 0; w < 2; ++w)
                    for (int n = 0; n < six.count[w]; ++n)
                    {
                        const ActorState &bot = g.actors.states[six.slot[w][n]];
                        const AiMonster *m = aiOf(g.actors, six.slot[w][n]);
                        std::printf(" [%c%d %d,%d h%d %s g%d,%d]", w == 0 ? 'T' : 'C', n, whole(bot.pos[0]),
                                    whole(bot.pos[1]), static_cast<int>(bot.health),
                                    m && m->schedule ? m->schedule->name : "-", m ? whole(m->moveGoal[0]) : 0,
                                    m ? whole(m->moveGoal[1]) : 0);
                    }
                std::printf("\n");
            }
            g.drain();
            const long long took = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - before).count();
            worst = took > worst ? took : worst;
        }
        std::printf("  rounds %d; the slowest frame %lld us\n", rounds, worst);
        int rifles = 0;
        for (int w = 0; w < 2; ++w)
            for (int n = 0; n < six.count[w]; ++n)
            {
                const cs::BotPurse &p = cs::brain.purse[w][n];
                std::printf("  bot %d %d: $%d, gun %s\n", w, n, p.money, cs::kBotGunModels[p.gun]);
                rifles += p.gun == cs::kGunAk47 || p.gun == cs::kGunM4a1;
            }
        std::printf("  rifles at the rounds' starts %d; held now %d\n", boughtRifles, rifles);
        check(boughtRifles > 0, "after a few rounds some bots have bought rifles");
        std::printf("  grenades held at the rounds' starts %d; thrown %d\n", boughtGrenades, cs::botGrenadesThrown);
        // (Whether one is thrown in these few rounds is the fights' luck.)
        check(boughtGrenades > 0, "the bots buy grenades");

        hldata::csGrenadeHook = nullptr;
        cs::grenadesClear();
        int kills = 0, deaths = 0;
        for (int w = 0; w < 2; ++w)
            for (int n = 0; n < six.count[w]; ++n)
            {
                kills += cs::scores.bot[w][n].kills;
                deaths += cs::scores.bot[w][n].deaths;
                std::printf("  %s: %d kills, %d deaths\n", cs::botName(w == 0 ? cs::kTeamT : cs::kTeamCT, n),
                            cs::scores.bot[w][n].kills, cs::scores.bot[w][n].deaths);
            }
        check(deaths > 0 && kills > 0 && kills <= deaths, "kills and deaths are counted");
        for (int s = 0; s < cs::brain.map.sites; ++s)
            for (int l = 0; l < cs::brain.lanes[s]; ++l)
            {
                std::printf("  site %d: way %d:", s, l + 1);
                for (int k = 0; k < cs::brain.laneSteps[s][l]; ++k)
                {
                    const fx *pt = cs::brain.lane[s][l][k];
                    const int at = pathNearest(g.actors, *g.logic.world, pt);
                    const int place = at >= 0 ? pathNodePlace(g.actors.nodes[at]) : 0;
                    std::printf(" %d,%d (%s)", whole(pt[0]), whole(pt[1]),
                                place > 0 && place <= static_cast<int>(g.navMesh.places.size()) ? g.navMesh.places[place - 1].c_str() : "-");
                }
                std::printf("\n");
            }
        // (A probe of the walk through dust2's double doors.)
        if (std::getenv("CSBOTS_PROBE") != nullptr)
        {
            for (int x = -520; x <= -260; x += 20)
            {
                std::printf("  x %d:", x);
                for (int y0 = 1450; y0 <= 1950; y0 += 50)
                {
                    ActorState p0;
                    p0.pos[0] = fxFromInt(x);
                    p0.pos[1] = fxFromInt(y0);
                    p0.pos[2] = fxFromInt(-100);
                    const bool f0 = actorToFloor(*g.logic.world, p0, fxFromInt(24), fxFromInt(96));
                    ActorState p1 = p0;
                    p1.pos[1] = fxFromInt(y0 + 50);
                    const bool f1 = actorToFloor(*g.logic.world, p1, fxFromInt(24), fxFromInt(96));
                    const bool ok = f0 && f1 && walkClear(*g.logic.world, nullptr, -1, kWalkStanding, p0.pos, p1.pos, 8);
                    std::printf("%c", !f0 ? 'x' : (ok ? '.' : '#'));
                }
                std::printf("\n");
            }
        }
        // (Each way's route between its points, node by node.)
        if (std::getenv("CSBOTS_ROUTES") != nullptr)
            for (int s = 0; s < cs::brain.map.sites; ++s)
                for (int l = 0; l < cs::brain.lanes[s]; ++l)
                    for (int k = 0; k + 1 < cs::brain.laneSteps[s][l]; ++k)
                    {
                        int at = pathNearest(g.actors, *g.logic.world, cs::brain.lane[s][l][k]);
                        const int to = pathNearest(g.actors, *g.logic.world, cs::brain.lane[s][l][k + 1]);
                        std::printf("  route s%d w%d %d->%d:", s, l + 1, k, k + 1);
                        for (int guard = 0; at >= 0 && at != to && guard < 60; ++guard)
                        {
                            const int place = pathNodePlace(g.actors.nodes[at]);
                            std::printf(" %d,%d[%s]", whole(g.actors.nodes[at].pos[0]), whole(g.actors.nodes[at].pos[1]),
                                        place > 0 ? g.navMesh.places[place - 1].c_str() : "-");
                            const int next = pathStep(g.actors, at, to, kPathLinkHuman, false);
                            at = next == kNoPathNode ? -1 : next;
                        }
                        std::printf("\n");
                    }
        for (int h = 0; h < cs::brain.holds; ++h)
        {
            const int at = pathNearest(g.actors, *g.logic.world, cs::brain.hold[h]);
            const int place = at >= 0 ? pathNodePlace(g.actors.nodes[at]) : 0;
            std::printf("  hold %d: %d,%d,%d site %d%s (%s)\n", h, whole(cs::brain.hold[h][0]), whole(cs::brain.hold[h][1]),
                        whole(cs::brain.hold[h][2]), cs::brain.holdSite[h], cs::brain.holdEarly[h] ? " early" : "",
                        place > 0 && place <= static_cast<int>(g.navMesh.places.size()) ? g.navMesh.places[place - 1].c_str() : "-");
        }
        for (int s = 0; s < cs::brain.map.sites; ++s)
            std::printf("  site %d: gathering at %d %d (%d)\n", s, whole(cs::brain.stage[s][0]), whole(cs::brain.stage[s][1]),
                        cs::brain.stageOk[s]);
        for (int s = 0; s < cs::brain.map.sites; ++s)
            std::printf("  site %d: the other way %s, at %d %d %d\n", s, cs::brain.viaOk[s] ? "found" : "none",
                        whole(cs::brain.via[s][0]), whole(cs::brain.via[s][1]), whole(cs::brain.via[s][2]));
        check(cs::brain.viaMade && (cs::brain.viaOk[0] || cs::brain.viaOk[1]), "another way to a site is found");
        check(rounds >= 1, "rounds go by");
    }
    // The player a terrorist, giving orders to three bots: GO B (they go
    // in there), FOLLOW ME (back to the player), HOLD (they stay put).
    {
        const EntityRecord *tStart = nullptr;
        check(cs::teamStart(g.view, cs::kTeamT, 0, tStart), "a terrorist start");
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = tStart->origin[a];
        g.player.health = 100;
        cs::Setup three;
        three.tBots = 3;
        three.ctBots = 0;
        cs::BotRoster mine;
        cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamT, three, mine);
        check(mine.count[1] == 3, "three bots with the player");
        cs::Round ro;
        cs::PlayerCs po;
        po.team = cs::kTeamT;
        ro.bombMap = true;
        ro.number = 3;
        ro.phase = cs::Phase::Live;
        ro.clockMs = ro.roundMs;
        cs::botsNewRound(g.view, g.actors, mine, cs::kTeamT, ro, cs::setup);
        check(cs::teamOrder[0] == cs::Order::Auto && cs::teamOrder[1] == cs::Order::Auto, "a round starts under AUTO");
        auto tick = [&]()
        {
            logicTick(g.logic, g.player, 33);
            g.logic.viewTurn = 0;
            monstersTick(g.logic, g.player, 33);
            ro.liveMs += 33;
            ro.clockMs = ro.clockMs > 33 ? ro.clockMs - 33 : ro.roundMs; // (no end to it here)
            cs::botsThink(g.logic, g.player, g.view, mine, cs::kTeamT, ro, po, 33);
            g.drain();
            if (std::getenv("CSBOTS_ORDERS") != nullptr && ro.liveMs % 2000 < 33)
            {
                std::printf("  %ds order %d player %d,%d:", ro.liveMs / 1000, static_cast<int>(cs::teamOrder[0]),
                            whole(g.player.origin[0]), whole(g.player.origin[1]));
                for (int l = 0; l < g.actors.linkCount; ++l)
                    if (g.actors.links[l].brush != kNoBrush)
                    {
                        for (int i = 0; i < g.logic.recordCount; ++i)
                            if (g.logic.records[i].brush == g.actors.links[l].brush)
                                std::printf(" door%d(k%d m%d shut%d name%d fl%x)", i, g.logic.records[i].kind,
                                            static_cast<int>(g.logic.states[i].move), g.actors.linkShut ? g.actors.linkShut[l] : 9,
                                            g.logic.records[i].name, g.logic.records[i].flags);
                        break;
                    }
                if (std::getenv("CSBOTS_DOORPROBE") != nullptr)
                {
                    const int pts[][2] = {{48, -1248}, {317, -1292}, {336, -1296}, {672, -1344}, {336, -1250}, {420, -1270}};
                    for (int i = 0; i < 6; ++i)
                        for (int j = i + 1; j < 6; ++j)
                        {
                            const fx p0[3] = {fxFromInt(pts[i][0]), fxFromInt(pts[i][1]), fxFromInt(-416)};
                            const fx p1[3] = {fxFromInt(pts[j][0]), fxFromInt(pts[j][1]), fxFromInt(-416)};
                            std::printf(" %d-%d:%d", i, j, walkClear(*g.logic.world, nullptr, -1, kWalkStanding, p0, p1, 16) ? 1 : 0);
                        }
                }
                for (int n = 0; n < mine.count[1]; ++n)
                {
                    const ActorState &bot = g.actors.states[mine.slot[1][n]];
                    const AiMonster *m = aiOf(g.actors, mine.slot[1][n]);
                    const cs::BotMind &mind = cs::brain.mind[1][n];
                    std::printf(" [%d,%d,%d v%d c%d t%d %s g%d,%d h%d f%d d%d]", whole(bot.pos[0]), whole(bot.pos[1]), whole(bot.pos[2]), mind.viaStep, mind.crawlDuct, mind.tries,
                                m && m->schedule ? m->schedule->name : "-", m ? whole(m->moveGoal[0]) : 0,
                                m ? whole(m->moveGoal[1]) : 0, mind.heardMs, mind.faceMs, mind.detourMs);
                }
                std::printf("\n");
            }
        };
        auto order = [&](cs::Order o) { cs::giveOrder(cs::kTeamT, o, kAiPlayer); };
        auto pos = [&](int n) { return g.actors.states[mine.slot[1][n]].pos; };
        const int b = cs::brain.siteB;
        const fx *bSpot = cs::brain.map.spot[b][0];
        for (int t = 0; t < 1000; t += 33)
            tick();
        order(cs::Order::GoB);
        int inB = -1;
        for (int t = 0; t < 80000 && inB < 0; t += 33)
        {
            tick();
            int there = 0;
            for (int n = 0; n < 3; ++n)
                there += cs::flatApart(pos(n), bSpot) < fxFromInt(800) ? 1 : 0;
            if (there == 3)
                inB = t;
        }
        std::printf("  GO B (site %d, A is %d): all three there at %d ms;", b, cs::brain.siteA, inB);
        for (int n = 0; n < 3; ++n)
            std::printf(" %d,%d", whole(pos(n)[0]), whole(pos(n)[1]));
        std::printf("\n");
        check(!dust2 || cs::brain.siteA != cs::brain.siteB, "dust2's A and B known");
        check(inB >= 0, "GO B: the bots go to B");
        order(cs::Order::FollowMe);
        const fx feet[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] - fxFromInt(36)};
        int withMe = -1;
        for (int t = 0; t < 80000 && withMe < 0; t += 33)
        {
            tick();
            int near = 0;
            for (int n = 0; n < 3; ++n)
                near += cs::flatApart(pos(n), feet) < fxFromInt(400) ? 1 : 0;
            if (near == 3)
                withMe = t;
        }
        std::printf("  FOLLOW ME: all three by the player at %d ms\n", withMe);
        check(withMe >= 0, "FOLLOW ME: the bots come to the player");
        // (The player walked off to mid: after them.)
        const bool inferno = mapName == "de_inferno";
        fx toMid[3] = {fxFromInt(dust2 ? -150 : (inferno ? 528 : -400)), fxFromInt(dust2 ? 600 : (inferno ? 576 : -750)),
                       fxFromInt(dust2 ? 0 : (inferno ? 96 : -416))};
        const int midNode = pathNearest(g.actors, *g.logic.world, toMid);
        int followed = -1;
        if (midNode >= 0)
        {
            // (Walked there node by node, a node each half second or so.)
            int at = pathNearest(g.actors, *g.logic.world, feet);
            for (int steps = 0; at >= 0 && at != midNode && steps < 200; ++steps)
            {
                const int next = pathStep(g.actors, at, midNode, kPathLinkHuman, false);
                if (next == kNoPathNode)
                    break;
                // (At a walk: some 40 units each 5 frames.)
                const fx *from = g.actors.nodes[at].pos, *to = g.actors.nodes[next].pos;
                const int parts = whole(cs::flatApart(from, to)) / 40 + 1;
                for (int part = 1; part <= parts; ++part)
                {
                    for (int a = 0; a < 3; ++a)
                        g.player.origin[a] = from[a] + (to[a] - from[a]) * part / parts + (a == 2 ? fxFromInt(36) : 0);
                    for (int f = 0; f < 5; ++f)
                        tick();
                }
                at = next;
            }
            const fx now[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] - fxFromInt(36)};
            for (int t = 0; t < 40000 && followed < 0; t += 33)
            {
                tick();
                int near = 0;
                for (int n = 0; n < 3; ++n)
                    near += cs::flatApart(pos(n), now) < fxFromInt(450) ? 1 : 0;
                if (near == 3)
                    followed = t;
            }
        }
        std::printf("  ... the player off to mid: followed (%d ms after)\n", followed);
        check(followed >= 0, "FOLLOW ME: they follow the player");
        order(cs::Order::Hold);
        fx at[3][3];
        for (int n = 0; n < 3; ++n)
            for (int a = 0; a < 3; ++a)
                at[n][a] = pos(n)[a];
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = tStart->origin[a];
        fx most = 0;
        for (int t = 0; t < 12000; t += 33)
        {
            tick();
            for (int n = 0; n < 3; ++n)
            {
                const fx d = cs::flatApart(pos(n), at[n]);
                most = d > most ? d : most;
            }
        }
        std::printf("  HOLD: the most any moved %d\n", whole(most));
        check(most < fxFromInt(200), "HOLD: they stay put, the player gone");
        // The map's own ways (the radio's, after the orders): each told in
        // a round of its own, from the start -- all by it, to its site.
        // (The player out of their way: below the map.)
        g.player.origin[2] -= fxFromInt(8000);
        for (int kk = 0; kk < cs::radioWays; ++kk)
        {
            // (CSBOTS_WAYS_BACK: the last first.)
            const int k = std::getenv("CSBOTS_WAYS_BACK") != nullptr ? cs::radioWays - 1 - kk : kk;
            ro.number += 1;
            ro.phase = cs::Phase::Live;
            ro.liveMs = 0;
            ro.clockMs = ro.roundMs;
            cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamT, three, mine);
            cs::botsNewRound(g.view, g.actors, mine, cs::kTeamT, ro, cs::setup);
            for (int t = 0; t < 500; t += 33)
                tick();
            order(static_cast<cs::Order>(static_cast<int>(cs::Order::Way0) + k));
            const int route = cs::brain.wayRoute[k];
            const int site = cs::brain.routeSite[route];
            const fx *spot = cs::brain.map.spot[site][0];
            std::uint32_t places[3] = {};
            bool crawled = false;
            int there = -1;
            for (int t = 0; t < 90000 && there < 0; t += 33)
            {
                tick();
                int in = 0;
                for (int n = 0; n < 3; ++n)
                {
                    crawled = crawled || cs::brain.mind[1][n].crawlDuct >= 0;
                    const int at = pathNearest(g.actors, *g.logic.world, pos(n));
                    if (at >= 0)
                        places[n] |= 1u << pathNodePlace(g.actors.nodes[at]);
                    const fx dz = pos(n)[2] - spot[2];
                    in += cs::flatApart(pos(n), spot) < fxFromInt(500) && dz < fxFromInt(100) && dz > -fxFromInt(100) ? 1 : 0;
                }
                if (in == 3)
                    there = t;
            }
            std::printf("  %s (site %d, way %d): all there at %d ms%s;", cs::radioWayName[k], site,
                        cs::brain.routeLane[route], there, crawled ? ", a crawl" : "");
            for (int n = 0; n < 3; ++n)
            {
                std::printf(" [");
                for (int pl = 1; pl < 32; ++pl)
                    if (places[n] & (1u << pl))
                        std::printf(" %s", pl <= static_cast<int>(g.navMesh.places.size()) ? g.navMesh.places[pl - 1].c_str() : "?");
                std::printf(" ]");
            }
            std::printf("\n");
            check(there >= 0, "a way told: all by it, to its site");
            if (std::string(cs::radioWayName[k]) == "VENT")
                check(crawled, "the vent: crawled through");
        }
        order(cs::Order::Auto);
        g.player.health = 100;

        // The player a counter-terrorist (dead: their radio still theirs),
        // with three bots: BOMB A -- all to A; then each of the map's places
        // to hold, in a round of its own: the bots at its holds.
        cs::Setup ct3;
        ct3.tBots = 0;
        ct3.ctBots = 3;
        cs::BotRoster cts;
        cs::Round rc;
        cs::PlayerCs pc;
        pc.team = cs::kTeamCT;
        rc.bombMap = true;
        rc.number = 9;
        g.player.health = 0;
        auto ctRound = [&]()
        {
            rc.number += 1;
            rc.phase = cs::Phase::Live;
            rc.liveMs = 0;
            rc.clockMs = rc.roundMs;
            cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamCT, ct3, cts);
            cs::botsNewRound(g.view, g.actors, cts, cs::kTeamCT, rc, cs::setup);
        };
        auto ctTick = [&]()
        {
            logicTick(g.logic, g.player, 33);
            g.logic.viewTurn = 0;
            monstersTick(g.logic, g.player, 33);
            rc.liveMs += 33;
            rc.clockMs = rc.clockMs > 33 ? rc.clockMs - 33 : rc.roundMs;
            cs::botsThink(g.logic, g.player, g.view, cts, cs::kTeamCT, rc, pc, 33);
            g.drain();
            if (std::getenv("CSBOTS_CT") != nullptr && rc.liveMs % 3000 < 33)
            {
                std::printf("  ct %ds:", rc.liveMs / 1000);
                for (int n = 0; n < cts.count[1]; ++n)
                {
                    const ActorState &bot = g.actors.states[cts.slot[1][n]];
                    const AiMonster *m = aiOf(g.actors, cts.slot[1][n]);
                    const cs::BotMind &mind = cs::brain.mind[1][n];
                    std::printf(" [%d,%d,%d %s g%d,%d,%d st%d e%d]", whole(bot.pos[0]), whole(bot.pos[1]), whole(bot.pos[2]),
                                m && m->schedule ? m->schedule->name : "-", whole(mind.goal[0]), whole(mind.goal[1]),
                                whole(mind.goal[2]), mind.stuckTimes, m ? m->enemy : -9);
                }
                std::printf("\n");
            }
        };
        auto ctPos = [&](int n) { return g.actors.states[cts.slot[1][n]].pos; };
        ctRound();
        check(cts.count[1] == 3, "three counter-terrorist bots with the player");
        for (int t = 0; t < 1000; t += 33)
            ctTick();
        cs::giveOrder(cs::kTeamCT, cs::Order::GoA, kAiPlayer);
        const int aSite = cs::brain.siteA;
        int inA = -1;
        for (int t = 0; t < 60000 && inA < 0; t += 33)
        {
            ctTick();
            int there = 0;
            for (int n = 0; n < 3; ++n)
            {
                bool near = false;
                for (int k = 0; k < cs::brain.map.spots[aSite]; ++k)
                    near = near || cs::flatApart(ctPos(n), cs::brain.map.spot[aSite][k]) < fxFromInt(700);
                for (int h = 0; h < cs::brain.holds; ++h)
                    near = near || (cs::brain.holdSite[h] == aSite && cs::flatApart(ctPos(n), cs::brain.hold[h]) < fxFromInt(300));
                there += near ? 1 : 0;
            }
            if (there == 3)
                inA = t;
        }
        std::printf("  BOMB A (a counter-terrorist's): all three at A at %d ms;", inA);
        for (int n = 0; n < 3; ++n)
            std::printf(" %d,%d,%d", whole(ctPos(n)[0]), whole(ctPos(n)[1]), whole(ctPos(n)[2]));
        std::printf(" (A's spots:");
        for (int k = 0; k < cs::brain.map.spots[aSite]; ++k)
            std::printf(" %d,%d", whole(cs::brain.map.spot[aSite][k][0]), whole(cs::brain.map.spot[aSite][k][1]));
        std::printf(")\n");
        check(inA >= 0, "BOMB A: the counter-terrorists go to A");
        for (int k = 0; k < cs::ctWays; ++k)
        {
            ctRound();
            for (int t = 0; t < 500; t += 33)
                ctTick();
            cs::giveOrder(cs::kTeamCT, static_cast<cs::Order>(static_cast<int>(cs::Order::Way0) + k), kAiPlayer);
            int held = -1;
            for (int t = 0; t < 70000 && held < 0; t += 33)
            {
                ctTick();
                int there = 0;
                for (int n = 0; n < 3; ++n)
                {
                    const int h = cs::brain.ctWayHold[k][n % 2];
                    there += cs::flatApart(ctPos(n), cs::brain.hold[h]) < fxFromInt(260) ? 1 : 0;
                }
                if (there == 3)
                    held = t;
            }
            std::printf("  %s (a counter-terrorist's): all three there at %d ms;", cs::ctWayName[k], held);
            for (int n = 0; n < 3; ++n)
            {
                const int h = cs::brain.ctWayHold[k][n % 2];
                std::printf(" %d,%d,%d (hold %d at %d,%d,%d)", whole(ctPos(n)[0]), whole(ctPos(n)[1]), whole(ctPos(n)[2]), h,
                            whole(cs::brain.hold[h][0]), whole(cs::brain.hold[h][1]), whole(cs::brain.hold[h][2]));
            }
            std::printf("\n");
            check(held >= 0, "a place told: the counter-terrorists hold it");
        }
        cs::giveOrder(cs::kTeamCT, cs::Order::Auto, kAiPlayer);
        g.player.health = 100;
    }
    // A match between consoles, the host's side of it (HlCsNet.hpp): its
    // bots make the teams up to four a side, in the seats nobody has, and
    // are the players in them -- seen where they are, shot by the others,
    // shooting them.
    {
        auto held = std::make_unique<NetMatch>();
        NetMatch &m = *held;
        m.role = NetMatch::Role::Host;
        m.me = 0;
        m.playing = true;
        m.rounds = true;
        NetRules rules;
        rules.mode = kNetModeTeamplay;
        netSetRules(m, rules);
        m.firstActor = static_cast<int>(g.view.actorCount);
        m.model = 0;
        std::printf("net: actors %d, the players' from %d\n", g.actors.count, m.firstActor);
        // (The host a counter-terrorist; a client, in seat 1, a terrorist.)
        m.profiles[0].known = m.profiles[1].known = true;
        m.profiles[0].team = 2;
        m.profiles[1].team = 1;
        m.host.players[1].flags = kNetHere | kNetInGame | kNetAlive;
        m.botTeamSize = 4;
        cs::Setup ns;
        int startIndex = -1;
        cs::netBegin(m, ns, startIndex);
        check(ns.team == cs::kTeamCT && startIndex == 0, "net: the host a counter-terrorist, at the first start");
        check(cs::netStartIndex(m, 1) == 0, "net: the client the terrorists' first");
        cs::netFillBots(m, ns);
        int bots = 0, tSeat = -1, ctSeat = -1;
        for (int s = 0; s < kNetPlayers; ++s)
        {
            if (!m.bot[s])
                continue;
            ++bots;
            std::printf("  seat %d: %s, team %d\n", s, m.names[s], m.profiles[s].team);
            if (cs::netRounds.botTeam[s] == cs::kTeamT && tSeat < 0)
                tSeat = s;
            if (cs::netRounds.botTeam[s] == cs::kTeamCT && ctSeat < 0)
                ctSeat = s;
        }
        check(bots == 6 && !m.bot[0] && !m.bot[1] && cs::netRounds.botSetup.tBots == 3 &&
                  cs::netRounds.botSetup.ctBots == 3,
              "net: four a side -- three bots to each, in the six seats left");
        check(cs::netStartIndex(m, 1) == 0, "net: the bots' seats not counted for a player's start");
        cs::BotRoster nr;
        cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamCT, cs::netRounds.botSetup, nr);
        check(nr.count[0] == 3 && nr.count[1] == 3, "net: the host wakes them, three a side");
        cs::Round rr;
        cs::PlayerCs pr;
        pr.team = cs::kTeamCT;
        rr.bombMap = true;
        rr.phase = cs::Phase::Live;
        cs::botsNewRound(g.view, g.actors, nr, cs::kTeamCT, rr, cs::netRounds.botSetup);
        cs::netBotsWoken(m, nr, cs::kTeamCT);
        bool mapped = true;
        for (int s = 0; s < kNetPlayers; ++s)
            mapped = mapped && (!m.bot[s] || (cs::netRounds.seatActor[s] >= 0 && cs::netRounds.botLives[s] == 1));
        check(mapped, "net: each bot's seat its actor");
        cs::netBotsTick(m, g.logic, 33);
        bool seen = true;
        for (int s = 0; s < kNetPlayers; ++s)
        {
            if (!m.bot[s])
                continue;
            const NetPlayer &f = m.host.players[s];
            const ActorState &b = g.actors.states[cs::netRounds.seatActor[s]];
            std::printf("  seat %d: actor %d (active %d ai %d health %d at %d %d %d), frame flags %x health %d at %d %d %d\n", s,
                        cs::netRounds.seatActor[s], b.active, b.ai, static_cast<int>(b.health), whole(b.pos[0]),
                        whole(b.pos[1]), whole(b.pos[2]), f.flags, f.health, f.pos[0] / 4, f.pos[1] / 4, f.pos[2] / 4);
            seen = seen && (f.flags & kNetAlive) && (f.flags & kNetBot) && f.health == (b.health > 0 ? b.health : cs::kBotHealth) &&
                   f.pos[0] == (b.pos[0] >> 10) && f.pos[2] == ((b.pos[2] + fxFromInt(36)) >> 10);
        }
        check(seen, "net: each bot the player in its seat, alive, where it is");
        // How they take to the client's player: a terrorist's mate, a
        // counter-terrorist's enemy.
        check(cs::netBotRelation(cs::netRounds.seatActor[tSeat], m.firstActor + 1, kRelHate) == kRelAlly &&
                  cs::netBotRelation(cs::netRounds.seatActor[ctSeat], m.firstActor + 1, kRelAlly) == kRelHate,
              "net: the bots by team, to another console's player");
        // The client's shots at a counter-terrorist bot: done to it, and its
        // death told as the client's kill.
        NetEvent shot = netEvent(NetEventType::Hit, ctSeat, 400, cs::netRounds.botLives[ctSeat]);
        shot.from = 1;
        m.roundEvents[m.roundEventCount++] = shot;
        const int victim = cs::netRounds.seatActor[ctSeat];
        cs::netBotHitsTake(m, g.logic);
        check(m.roundEventCount == 0 && !cs::botUp(g.actors.states[victim]), "net: a client's hits on a bot, taken");
        m.host.out = NetOutbox();
        cs::netBotsTick(m, g.logic, 33);
        bool told = false;
        for (int i = 0; i < m.host.out.count; ++i)
        {
            const NetEvent &e = m.host.out.events[(m.host.out.head + i) % NetOutbox::kMost];
            told = told || (e.type == NetEventType::Died && e.from == ctSeat && e.a == 1);
        }
        check(told && m.frags[1] == 1 && m.deaths[ctSeat] == 1 && !(m.host.players[ctSeat].flags & kNetAlive),
              "net: the bot's death told, the client's kill");
        // A terrorist bot's hit on another console's counter-terrorist (seat
        // 1 now one, for this): said as from the bot's seat.
        m.profiles[1].team = 2;
        ActorState &other = g.actors.states[m.firstActor + 1];
        other = ActorState();
        other.active = true;
        other.kind = static_cast<std::uint8_t>(ActorKind::Player);
        other.health = static_cast<std::int16_t>(kNetActorHealth - 27);
        csActorHitBy[m.firstActor + 1] = cs::netRounds.seatActor[tSeat];
        m.host.out = NetOutbox();
        cs::netBotsTick(m, g.logic, 33);
        bool hit = false;
        for (int i = 0; i < m.host.out.count; ++i)
        {
            const NetEvent &e = m.host.out.events[(m.host.out.head + i) % NetOutbox::kMost];
            hit = hit || (e.type == NetEventType::Hit && e.from == tSeat && e.a == 1 && e.v[0] == 27);
        }
        check(hit && other.health == kNetActorHealth, "net: a bot's hit on another's player, said as the bot's");
        // The host's own player, shot by a bot: theirs, should they die.
        csPlayerHitBy = cs::netRounds.seatActor[tSeat];
        cs::netBotsTick(m, g.logic, 33);
        check(m.blamed == tSeat && m.blameMs > 0 && csPlayerHitBy == -1, "net: the host shot by a bot: the bot's");
        // The bomb, the host's to give: a bot's in hand, as alone.
        rr.phase = cs::Phase::Live;
        rr.clockMs = rr.roundMs;
        cs::RoundIn in;
        in.alive = true;
        in.team = cs::kTeamCT;
        in.ms = 33;
        cs::netRoundTick(m, rr, in, pr);
        std::printf("  the bomb: seat %d, actor %d; phase %d\n", cs::netRounds.carrier, rr.botBomb, static_cast<int>(rr.phase));
        check(cs::netRounds.carrier == tSeat && rr.botBomb == cs::netRounds.seatActor[tSeat] && !rr.hasBomb,
              "net: a bot with the bomb, as alone");
        cs::netRounds = cs::NetRounds();
        g.actors.states[m.firstActor + 1].active = false;
    }

    // Counter-Strike's wounds: an AK's 36 four times in the head, three
    // quarters in a leg; through a vest, as its 1.55 lets through.
    check(cs::botTakes(-1, kAiPlayer, 36, 1) == 144 && cs::botTakes(-1, kAiPlayer, 36, 6) == 27 &&
              cs::botTakes(-1, kAiPlayer, 36, 2) == 36,
          "hit groups as Counter-Strike's");
    {
        PlayerState p;
        p.health = 100;
        p.inv.armor = 100;
        hldata::csArmorPierce = 155;
        playerTakeDamage(p, 36);
        std::printf("  an AK round into a vest: health %d, armour %d\n", static_cast<int>(p.health), static_cast<int>(p.inv.armor));
        check(p.health == 73 && p.inv.armor == 95, "armour as Counter-Strike's");
        hldata::csArmorPierce = 100;
    }
    // Smoke (HlCsBots.hpp's grenades): a cloud hides what is behind it,
    // from a bot too. And the bomb's blast.
    {
        cs::Setup one;
        one.tBots = 1;
        one.ctBots = 0;
        cs::BotRoster roster;
        cs::wakeBots(g.view, g.logic, g.actors, cs::kTeamCT, one, roster);
        cs::Round rf;
        rf.bombMap = true;
        rf.phase = cs::Phase::Live;
        cs::botsNewRound(g.view, g.actors, roster, cs::kTeamCT, rf, cs::setup);
        for (int ms = 0; ms < 600; ms += 33)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
        }
        const ActorState &t0 = g.actors.states[roster.slot[0][0]];
        cs::grenadesClear();
        const fx at[3] = {t0.pos[0] + fxFromInt(300), t0.pos[1], t0.pos[2]};
        cs::smokeGoesOff(g.logic, at);
        cs::smokeTick(g.logic, g.player, 2000);
        const fx before[3] = {at[0] - fxFromInt(400), at[1], at[2] + fxFromInt(48)};
        const fx behind[3] = {at[0] + fxFromInt(400), at[1], at[2] + fxFromInt(48)};
        const fx aside[3] = {at[0] - fxFromInt(400), at[1] + fxFromInt(600), at[2] + fxFromInt(48)};
        const fx aside2[3] = {at[0] + fxFromInt(400), at[1] + fxFromInt(600), at[2] + fxFromInt(48)};
        check(cs::smokeBetween(before, behind) && !cs::smokeBetween(aside, aside2), "smoke between hides; beside, not");
        hldata::csSightHook = &cs::smokeHides;
        AiMonster *m = aiOf(g.actors, roster.slot[0][0]);
        const AiKind *kind = aiKindOf(t0.kind);
        if (m != nullptr && kind != nullptr)
        {
            AiCtx c = aiCtx(g.logic, g.player, roster.slot[0][0], *m, *kind);
            fx eye[3];
            aiEye(c, eye);
            const fx through[3] = {eye[0] + fxFromInt(600), eye[1], eye[2]};
            check(!aiVisible(c, through), "a bot does not see through the smoke");
        }
        cs::smokeTick(g.logic, g.player, cs::kSmokeGoneMs);
        check(!cs::smokeBetween(before, behind), "the smoke gone, after a while");
        hldata::csSightHook = nullptr;
        // A bot with the tactical shield: a round from in front stopped,
        // from behind not, nor in the head.
        {
            const int i = roster.slot[0][0];
            const std::uint8_t was = cs::botGunHeld[i];
            cs::botGunHeld[i] = static_cast<std::uint8_t>(cs::kGunShield + 1);
            fx forward[3];
            aiForward(t0.yaw, forward);
            cs::takeWorld = &g.actors;
            for (int k = 0; k < 3; ++k)
                cs::takePlayer[k] = t0.pos[k] + forward[k] * 300;
            const int front = cs::botTakes(i, kAiPlayer, 30, 2), head = cs::botTakes(i, kAiPlayer, 30, 1);
            for (int k = 0; k < 3; ++k)
                cs::takePlayer[k] = t0.pos[k] - forward[k] * 300;
            const int back = cs::botTakes(i, kAiPlayer, 30, 2);
            std::printf("  a shield bot shot: in front %d, the head %d, behind %d\n", front, head, back);
            check(front == 0 && head > 0 && back > 0, "a bot's shield stops rounds from in front, not the head, not behind");
            cs::botGunHeld[i] = was;
        }
        // An HE as 1.6's: 100 falling to nothing at 350, through walls; not
        // on the thrower's side, but on the thrower.
        {
            const int i = roster.slot[0][0];
            ActorState &me = g.actors.states[i];
            const int keep = me.health;
            const fx at[3] = {me.pos[0] + fxFromInt(100), me.pos[1], me.pos[2]};
            me.health = 100;
            const std::uint8_t team = cs::botTeamOf[i];
            cs::botTeamOf[200] = team; // (a team-mate, as far as the grenade knows)
            cs::heGoesOff(g.logic, g.player, at, 200);
            const int byMate = me.health;
            cs::heGoesOff(g.logic, g.player, at, kAiPlayer);
            const int byEnemy = me.health;
            me.health = 100;
            cs::heGoesOff(g.logic, g.player, at, static_cast<std::int16_t>(i));
            const int bySelf = me.health;
            cs::botTeamOf[200] = 0;
            std::printf("  an HE 100 units off: by a team-mate %d, by the player %d, by itself %d\n", byMate, byEnemy, bySelf);
            check(team != 0 && byMate == 100 && byEnemy < 100 && bySelf < 100, "an HE hurts the other side and its thrower, not team-mates");
            me.health = static_cast<std::int16_t>(keep);
        }
        // The bomb gone off by a bot: it dies, whichever side it is on;
        // those out of its reach do not feel it.
        {
            const fx at[3] = {t0.pos[0] + fxFromInt(40), t0.pos[1], t0.pos[2]};
            int before[2][cs::kBotSlotsPerSide] = {};
            for (int w = 0; w < 2; ++w)
                for (int n = 0; n < roster.count[w]; ++n)
                    before[w][n] = g.actors.states[roster.slot[w][n]].health;
            cs::bombGoesOff(g.logic, g.player, at, 500);
            monstersTick(g.logic, g.player, 33);
            bool near = false, farOk = true;
            for (int w = 0; w < 2; ++w)
                for (int n = 0; n < roster.count[w]; ++n)
                {
                    const ActorState &bot = g.actors.states[roster.slot[w][n]];
                    const double d = apart(at, bot.pos);
                    if (d < 300)
                        near = near || !cs::botUp(bot);
                    if (d > 1750 && bot.health != before[w][n])
                        farOk = false;
                }
            check(near && !cs::botUp(t0), "the bomb kills the terrorist bot beside it");
            check(farOk, "the bomb does not reach past its radius");
        }
    }
    std::printf("player health %d\n", static_cast<int>(g.player.health));
    if (g_failures == 0)
        std::printf("test_csbots: ok\n");
    return g_failures == 0 ? 0 : 1;
}
