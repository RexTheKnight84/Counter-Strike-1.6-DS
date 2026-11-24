#pragma once

// Counter-Strike between consoles: HlCsRound.hpp's rounds over a match
// (HlNetGame.hpp's NetMatch, played with `rounds`).
//
// The host's round is everyone's. It runs it as a game alone does
// (roundTick), the others counted from what each console says of its
// player (their side, whether they are alive), and says how it stands
// (CsRound) whenever it changes, and each second besides. A client runs its
// clock between, and takes the host's word as it comes: a new round (back
// to their start), the freeze over, who won (and what that pays).
//
// Whoever plants the bomb says where (CsPlant), and whoever defuses it says
// so (CsDefuse): the host passes it on, and the round is the host's to end.
// The bomb is the host's to give: to a terrorist, in turn by the round, and
// to another if they fall with it.
//
// The host's bots make the teams up (NetMatch::botTeamSize): each in a seat
// nobody has, run on the host as alone, and seen by the others as the
// player in its seat -- where it is, its gun, its shots, its death. What
// another console's player does to one is said to the host (Hit, as to a
// player), and the host does it to the bot; what a bot does to another
// console's player is said as from the bot's seat.
//
// std-only: the host tests play rounds with it (test_cs, test_csbots).

#include "HlCsBots.hpp"
#include "HlCsRound.hpp"
#include "HlNetGame.hpp"

namespace hldata::cs
{
    /// What a console keeps of the match's rounds.
    struct NetRounds
    {
        int saidMs = 0;     ///< the host: since it last said how the round stands
        Phase saidPhase = Phase::Freeze;
        int saidNumber = -1;
        int carrier = -1; ///< the player with the bomb (-1: nobody)
        int kills = 0;    ///< this console's player's kills, as each was paid
        bool defuseSaid = false; ///< (a client) its player's defuse, said: waiting on the host
        bool takeSaid = false;   ///< (a client) the bomb lying walked over, said: waiting on the host
        // (The host) the bomb lying, as last said (CsBomb): whether, and where.
        bool saidDropped = false;
        std::int16_t saidDrop[3] = {0, 0, 0};
        // The host's bots, by seat: the team's (0: no bot's), its place
        // among that team's bots (BotRoster's), the actor that is it this
        // round, its lives (NetPlayer::spawns), and whether it was up.
        std::uint8_t botTeam[kNetPlayers] = {};
        std::uint8_t botSlot[kNetPlayers] = {};
        std::int16_t seatActor[kNetPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};
        std::uint8_t botLives[kNetPlayers] = {};
        bool botUp[kNetPlayers] = {};
        fx botWas[kNetPlayers][3] = {}; ///< where each was a frame ago (how fast it goes)
        /// The bots each team has (for wakeBots: the host's setup, else).
        Setup botSetup;
        /// The match being played here (nullptr: none) -- for the bots'
        /// sides (netBotRelation), which the AI asks of with nothing else.
        const NetMatch *match = nullptr;
    };
    static_assert(kNetPlayers == 8, "NetRounds::seatActor's -1s");
    constinit inline NetRounds netRounds;

    inline constexpr int kNetKillReward = 300;
    inline constexpr int kNetSayMs = 1000;

    /// Player `p`'s team in the match.
    inline std::uint8_t netTeamOf(const NetMatch &match, int p)
    {
        return netSide(match, p) == 1 ? kTeamT : kTeamCT;
    }

    /// Which of their team's starts player `p` comes in at: their place
    /// among the players of their side, by seat -- the first at the first;
    /// the rest past the bots' (which have the next kBotSlotsPerSide).
    inline int netStartIndex(const NetMatch &match, int p)
    {
        int n = 0;
        for (int q = 0; q < p && q < kNetPlayers; ++q)
            if (netHere(match, q) && !(netPlayer(match, q).flags & kNetBot) && netSide(match, q) == netSide(match, p))
                ++n;
        return n == 0 ? 0 : kBotSlotsPerSide + n;
    }

    /// Whether player `p` is in the match and alive (this console's: by
    /// `meAlive`).
    inline bool netPlaying(const NetMatch &match, int p)
    {
        return p == match.me || ((netPlayer(match, p).flags & kNetHere) && (netPlayer(match, p).flags & kNetInGame));
    }
    inline bool netAlive(const NetMatch &match, int p, bool meAlive)
    {
        return p == match.me ? meAlive : netPlaying(match, p) && (netPlayer(match, p).flags & kNetAlive) != 0;
    }

    /// This console's player is (back) in the map: the match is told, as
    /// netSpawn tells it.
    inline void netSpawned(NetMatch &match)
    {
        ++match.spawns;
        match.wasAlive = true;
        match.deadMs = 0;
        match.blamed = kNetPlayers;
        match.blameMs = 0;
        netSay(match, netEvent(NetEventType::Spawned));
    }

    /// The match's first round, here: this console's player's team and
    /// start (the caller then puts them there: Player::spawn).
    inline void netBegin(NetMatch &match, Setup &setup, int &startIndex)
    {
        netRounds = NetRounds();
        netRounds.kills = match.me >= 0 && match.me < kNetPlayers ? match.kills[match.me] : 0;
        setup.team = netTeamOf(match, match.me);
        startIndex = netStartIndex(match, match.me);
        match.roundsBegun = true;
        netRounds.match = &match;
    }

    /// The host: the bomb to a terrorist alive -- in turn by the round, at
    /// a round's start; the first one alive after `from`, when its carrier
    /// has fallen.
    inline int netBombTo(const NetMatch &match, int number, bool meAlive, int after = -1)
    {
        int ts[kNetPlayers];
        int count = 0;
        for (int p = 0; p < kNetPlayers; ++p)
            if (netPlaying(match, p) && netTeamOf(match, p) == kTeamT && netAlive(match, p, meAlive))
                ts[count++] = p;
        if (count == 0)
            return -1;
        if (after < 0)
            return ts[number % count];
        for (int k = 0; k < count; ++k)
            if (ts[k] > after)
                return ts[k];
        return ts[0];
    }

    // ------------------------------------------------------------- the bots

    /// The host, at the match's start: each team made up to the team size
    /// with bots, in the seats nobody has -- named, on their team, said to
    /// everyone. (Into netRounds.botSetup: how many each team has.)
    inline void netFillBots(NetMatch &match, const Setup &setup)
    {
        netRounds.botSetup = setup;
        netRounds.botSetup.tBots = netRounds.botSetup.ctBots = 0;
        netRounds.botSetup.money = 0; // (everyone starts with the same)
        if (match.role != NetMatch::Role::Host || match.botTeamSize == 0)
            return;
        for (const std::uint8_t team : {kTeamT, kTeamCT})
        {
            int humans = 0;
            for (int p = 0; p < kNetPlayers; ++p)
                humans += netHere(match, p) && !match.bot[p] && netTeamOf(match, p) == team ? 1 : 0;
            int want = match.botTeamSize - humans;
            want = want < 0 ? 0 : (want > kBotSlotsPerSide ? kBotSlotsPerSide : want);
            int made = 0;
            for (int s = 1; s < kNetPlayers && made < want; ++s)
            {
                if (s == match.me || netHere(match, s) || match.bot[s])
                    continue;
                match.bot[s] = true;
                NetProfile &profile = match.profiles[s];
                profile = NetProfile();
                profile.known = true;
                profile.team = team == kTeamT ? 1 : 2;
                std::snprintf(match.names[s], kNetTextBytes, "BOT %s", botName(team, made));
                NetPlayer &seat = match.host.players[s];
                seat = NetPlayer();
                seat.flags = kNetHere | kNetInGame | kNetBot;
                netRounds.botTeam[s] = team;
                netRounds.botSlot[s] = static_cast<std::uint8_t>(made);
                netRounds.seatActor[s] = -1;
                netRounds.botLives[s] = 0;
                netRounds.botUp[s] = false;
                NetEvent roster;
                roster.type = NetEventType::Roster;
                roster.a = static_cast<std::uint8_t>(s);
                std::memcpy(roster.text, match.names[s], kNetTextBytes);
                match.host.out.add(roster);
                NetEvent look = netProfileEvent(profile);
                look.from = static_cast<std::uint8_t>(s);
                match.host.out.add(look);
                match.host.out.add(netEvent(NetEventType::Team, s, profile.team));
                ++match.profileStamp;
                ++made;
            }
            (team == kTeamT ? netRounds.botSetup.tBots : netRounds.botSetup.ctBots) = static_cast<std::uint8_t>(made);
        }
    }

    /// The host, its bots just woken (a round's start): which actor each
    /// seat's is now -- another life of it, for the others to put it there
    /// and not glide it.
    inline void netBotsWoken(const NetMatch &match, const BotRoster &bots, std::uint8_t hostTeam)
    {
        for (int s = 0; s < kNetPlayers; ++s)
        {
            if (!match.bot[s])
                continue;
            const int side = netRounds.botTeam[s] == hostTeam ? 1 : 0;
            const int n = netRounds.botSlot[s];
            netRounds.seatActor[s] = static_cast<std::int16_t>(n < bots.count[side] ? bots.slot[side][n] : -1);
            ++netRounds.botLives[s];
            netRounds.botUp[s] = netRounds.seatActor[s] >= 0;
            if (netRounds.seatActor[s] >= 0 && netRounds.seatActor[s] < 256)
                csActorHitBy[netRounds.seatActor[s]] = -1;
        }
    }

    /// csRelationHook: how the host's bot `self` takes to another console's
    /// player here (actor `target`) -- hated, of the other team; else an
    /// ally. (Anything else: as the game's table has it.)
    inline int netBotRelation(int self, int target, int base)
    {
        const NetMatch *match = netRounds.match;
        if (match == nullptr || match->role != NetMatch::Role::Host || !match->playing || match->firstActor < 0 ||
            target < match->firstActor || target >= match->firstActor + kNetPlayers)
            return base;
        for (int s = 0; s < kNetPlayers; ++s)
            if (match->bot[s] && netRounds.seatActor[s] == self)
                return netRounds.botTeam[s] == netTeamOf(*match, target - match->firstActor) ? kRelAlly : kRelHate;
        return base;
    }

    /// The seat of the one whose actor is `actor`: a bot's, or another
    /// console's player here (-1: neither -- this console's player is
    /// kAiPlayer, theirs).
    inline int netSeatOfActor(const NetMatch &match, int actor)
    {
        if (actor == kAiPlayer)
            return match.me;
        if (match.firstActor >= 0 && actor >= match.firstActor && actor < match.firstActor + kNetPlayers)
            return actor - match.firstActor;
        for (int s = 0; s < kNetPlayers; ++s)
            if (match.bot[s] && netRounds.seatActor[s] == actor)
                return s;
        return -1;
    }

    /// The host: the others' hits on its bots (their Hit events, kept for
    /// it: HlNetGame.hpp), done to them -- as from that player's actor here,
    /// whom the bot then knows for who hurt it.
    inline void netBotHitsTake(NetMatch &match, LogicWorld &w)
    {
        int kept = 0;
        for (int i = 0; i < match.roundEventCount; ++i)
        {
            const NetEvent &e = match.roundEvents[i];
            if (e.type != NetEventType::Hit)
            {
                match.roundEvents[kept++] = e;
                continue;
            }
            const int s = e.a;
            const int bot = s < kNetPlayers ? netRounds.seatActor[s] : -1;
            if (bot < 0 || !match.bot[s] || static_cast<std::uint8_t>(e.v[1]) != netRounds.botLives[s] ||
                netTeammates(match, e.from, s) || match.firstActor < 0 || e.from >= kNetPlayers)
                continue;
            const ActorState &actor = w.actors->states[bot];
            if (!actor.active || actor.ai >= kAiDying)
                continue;
            monsterHurt(w, bot, e.v[0], true, kHitGeneric, kDmgUnset, static_cast<std::int16_t>(match.firstActor + e.from),
                        match.glide[e.from].pos);
        }
        match.roundEventCount = kept;
    }

    /// The host, each frame just before the match's own (its frame out):
    /// each bot as a player in its seat -- where it is, its gun, alive or
    /// not, a shot fired -- its death told, with who did it; and its hits on
    /// the others' players here, said as its own.
    inline void netBotsTick(NetMatch &match, LogicWorld &w, int ms)
    {
        if (match.role != NetMatch::Role::Host)
            return;
        ActorWorld &a = *w.actors;
        // The host's own player, hurt by a bot: its doing, should they die
        // of it (as another's hit is: HlNetGame.hpp's blame).
        if (csPlayerHitBy >= 0)
        {
            const int by = netSeatOfActor(match, csPlayerHitBy);
            if (by >= 0 && by != match.me && match.bot[by])
            {
                match.blamed = by;
                match.blameMs = kNetBlameMs;
            }
            csPlayerHitBy = -1;
        }
        for (int s = 0; s < kNetPlayers; ++s)
        {
            if (!match.bot[s])
                continue;
            NetPlayer &seat = match.host.players[s];
            seat.flags = kNetHere | kNetInGame | kNetBot;
            seat.spawns = netRounds.botLives[s];
            const int index = netRounds.seatActor[s];
            if (index < 0)
            {
                netRounds.botUp[s] = false;
                continue;
            }
            const ActorState &bot = a.states[index];
            // (Its health is given it as it first thinks: up, it has all of
            // it till then.)
            const bool up = botUp(bot);
            for (int k = 0; k < 3; ++k)
            {
                // (Where its middle is, as a player's: its feet, and 36 up.)
                const std::int32_t quarter = (bot.pos[k] + (k == 2 ? fxFromInt(36) : 0)) >> 10;
                seat.pos[k] = static_cast<std::int16_t>(quarter < -32768 ? -32768 : (quarter > 32767 ? 32767 : quarter));
                // (Eight units a second: what it went since the last frame.)
                const std::int64_t went = static_cast<std::int64_t>(bot.pos[k] - netRounds.botWas[s][k]) * 125 /
                                          (static_cast<std::int64_t>(4096) * (ms > 0 ? ms : 1));
                seat.velocity[k] = static_cast<std::int8_t>(!up || went < -127 ? (up ? -127 : 0) : (went > 127 ? 127 : went));
                netRounds.botWas[s][k] = bot.pos[k];
            }
            seat.yaw = bot.yaw;
            seat.pitch = 0;
            const int gun = botGunOf(index);
            seat.weapon = gun == kGunAk47 || gun == kGunM4a1 ? kWeaponMp5 : kWeaponGlock;
            seat.health = static_cast<std::uint8_t>(!up ? 0 : (bot.health <= 0 ? kBotHealth : (bot.health > 255 ? 255 : bot.health)));
            if (up)
                seat.flags |= kNetAlive | kNetOnGround;
            if (up && index < kBotGunActors && ears.botFired[index])
            {
                seat.flags |= kNetFiring;
                NetEvent shot = netEvent(NetEventType::Shot, seat.weapon, bot.yaw, 0);
                shot.from = static_cast<std::uint8_t>(s);
                match.host.out.add(shot);
            }
            if (netRounds.botUp[s] && !up && !match.over)
            {
                const int by = index < 256 ? csActorHitBy[index] : -1;
                const int seatBy = by >= 0 || by == kAiPlayer ? netSeatOfActor(match, by) : -1;
                const int killer = seatBy >= 0 && seatBy != s ? seatBy : kNetPlayers;
                NetEvent died = netEvent(NetEventType::Died, killer);
                died.from = static_cast<std::uint8_t>(s);
                match.host.out.add(died);
                ::hldata::detail::netNotice(match, NetEventType::Died, s, killer);
                ::hldata::detail::netCount(match, s, killer);
                ::hldata::detail::netScore(match, s, killer);
            }
            netRounds.botUp[s] = up;
        }
        // The bots' hits on the others' players here: the bot's to claim.
        for (int p = 0; p < kNetPlayers && match.firstActor >= 0; ++p)
        {
            if (p == match.me || match.bot[p])
                continue;
            const int index = match.firstActor + p;
            ActorState &actor = a.states[index];
            if (!actor.active || actor.kind != static_cast<std::uint8_t>(ActorKind::Player))
                continue;
            const int hurt = kNetActorHealth - actor.health;
            const int by = index < 256 ? csActorHitBy[index] : -1;
            const int s = hurt > 0 && by >= 0 ? netSeatOfActor(match, by) : -1;
            if (s < 0 || !match.bot[s])
                continue; // (not a bot's: the match's own claim, as this console's player's)
            actor.health = kNetActorHealth;
            if (!match.over && !netTeammates(match, s, p))
            {
                NetEvent hit = netEvent(NetEventType::Hit, p, hurt > 30000 ? 30000 : hurt, netPlayer(match, p).spawns);
                hit.from = static_cast<std::uint8_t>(s);
                match.host.out.add(hit);
            }
        }
    }

    /// This console's player puts the bomb they carry down at `feet` (the
    /// radio's DROP BOMB): there for a terrorist to take up -- the host's
    /// bots go for it. A client says so, and the host has the last word.
    inline void netDropBomb(NetMatch &match, Round &r, const fx *feet)
    {
        if (!r.hasBomb || r.phase != Phase::Live)
            return;
        r.hasBomb = false;
        r.bombDropped = true;
        for (int k = 0; k < 3; ++k)
            r.bombDrop[k] = feet[k];
        netRounds.carrier = -1;
        if (match.role != NetMatch::Role::Host)
            netSay(match, netEvent(NetEventType::CsDrop, 0, feet[0] >> 10, feet[1] >> 10, feet[2] >> 10));
    }

    /// The host: one of its bots has planted the bomb -- where, for the
    /// others (as its own player's plant is said).
    inline void netBotPlanted(NetMatch &match, const Round &r)
    {
        if (match.role != NetMatch::Role::Host)
            return;
        netRounds.carrier = -1;
        netSay(match, netEvent(NetEventType::CsPlant, 0, r.bomb[0] >> 10, r.bomb[1] >> 10, r.bomb[2] >> 10));
    }

    inline NetEvent netRoundEvent(const Round &r, int carrier)
    {
        const int phase = static_cast<int>(r.phase) | static_cast<int>(r.why) << 4 | r.winner << 8;
        const int clock = (r.phase == Phase::Planted ? r.bombMs : r.clockMs) / 100;
        const int losses = (carrier + 1) | (r.lossesT < 15 ? r.lossesT : 15) << 4 |
                           (r.lossesCT < 15 ? r.lossesCT : 15) << 8 | (r.plantedThisRound ? 1 << 12 : 0);
        return netEvent(NetEventType::CsRound, 0, phase, r.number, r.phaseMs / 100 < 32767 ? r.phaseMs / 100 : 32767,
                        clock < 0 ? 0 : clock, (r.scoreT & 0xFF) | (r.scoreCT & 0x7F) << 8, losses);
    }

    namespace detail
    {
        /// The bomb down at `at`, as a plant does it.
        inline void csPlantAt(Round &r, const fx *at)
        {
            r.phase = Phase::Planted;
            r.phaseMs = 0;
            r.hasBomb = false;
            r.plantedThisRound = true;
            r.bombMs = kC4Ms;
            r.beepMs = 0;
            r.plantMs = 0;
            for (int a = 0; a < 3; ++a)
                r.bomb[a] = at[a];
        }

        /// A client: the host's word of the round, taken.
        inline void csTakeRound(NetMatch &match, Round &r, const RoundIn &in, PlayerCs &cs, const NetEvent &e,
                                RoundOut &out)
        {
            const auto phase = static_cast<Phase>(e.v[0] & 0xF);
            const auto why = static_cast<Why>((e.v[0] >> 4) & 0xF);
            const auto winner = static_cast<std::uint8_t>((e.v[0] >> 8) & 0xF);
            const int number = e.v[1];
            const int carrier = (e.v[5] & 0xF) - 1;
            if (number != r.number)
            {
                // Another round: everyone back at their start (the game's
                // frame does it, as for a round alone).
                r.number = number;
                r.phase = Phase::Freeze;
                r.liveMs = 0;
                r.plantedThisRound = false;
                r.bombDropped = false;
                r.botBomb = -1;
                r.why = Why::None;
                r.winner = 0;
                r.plantMs = r.defuseMs = 0;
                c4InHand = false;
                netRounds.defuseSaid = false;
                netRounds.takeSaid = false;
                out.started = true;
            }
            if (phase == Phase::Over && r.phase != Phase::Over)
            {
                // Won: what it pays (endRound's, with the host's count of
                // the losses), and what is heard of it.
                r.lossesT = (e.v[5] >> 4) & 0xF;
                r.lossesCT = (e.v[5] >> 8) & 0xF;
                r.plantedThisRound = ((e.v[5] >> 12) & 1) != 0;
                const bool objective = why == Why::TargetBombed || why == Why::BombDefused;
                if (cs.team == winner)
                    addMoney(cs, objective ? kWinObjective : kWinElimination);
                else
                    addMoney(cs, lossBonus(cs.team == kTeamT ? r.lossesT : r.lossesCT) +
                                     (cs.team == kTeamT && r.plantedThisRound ? kPlantedLossBonus : 0));
                if (why == Why::TargetBombed)
                {
                    out.exploded = true;
                    if (in.alive && in.origin != nullptr)
                    {
                        const int dx = whole(in.origin[0] - r.bomb[0]), dy = whole(in.origin[1] - r.bomb[1]),
                                  dz = whole(in.origin[2] - r.bomb[2]);
                        out.blast = blastAt(r.bombRadius, isqrt(static_cast<std::uint32_t>(dx * dx + dy * dy + dz * dz)));
                    }
                }
                out.defused = why == Why::BombDefused;
                out.ended = true;
                r.plantMs = r.defuseMs = 0;
            }
            if (phase == Phase::Live && r.phase == Phase::Freeze)
            {
                out.unfrozen = true;
                r.liveMs = 0;
            }
            r.phase = phase; // (planted: where, its CsPlant said first)
            r.why = why;
            r.winner = winner;
            r.phaseMs = e.v[2] * 100;
            if (phase == Phase::Planted)
                r.bombMs = e.v[3] * 100;
            else if (phase == Phase::Live)
                r.clockMs = e.v[3] * 100;
            r.scoreT = e.v[4] & 0xFF;
            r.scoreCT = (e.v[4] >> 8) & 0x7F;
            netRounds.carrier = carrier;
            r.hasBomb = r.bombMap && carrier == match.me && (phase == Phase::Freeze || phase == Phase::Live);
        }
    } // namespace detail

    /// The round, a frame of a match: as roundTick, for the host (the
    /// others counted by side); as the host says, for a client.
    inline RoundOut netRoundTick(NetMatch &match, Round &r, RoundIn in, PlayerCs &cs)
    {
        const bool host = match.role == NetMatch::Role::Host;
        RoundOut out;
        // The others, by side.
        in.matesPlaying = in.matesAlive = in.enemiesPlaying = in.enemiesAlive = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            if (p == match.me || !netPlaying(match, p))
                continue;
            const bool mate = netTeamOf(match, p) == cs.team;
            const bool up = netAlive(match, p, in.alive);
            (mate ? in.matesPlaying : in.enemiesPlaying) += 1;
            (mate ? in.matesAlive : in.enemiesAlive) += up ? 1 : 0;
        }
        // Each kill of this console's player's paid.
        if (match.me >= 0 && match.me < kNetPlayers && match.kills[match.me] > netRounds.kills)
        {
            addMoney(cs, kNetKillReward * (match.kills[match.me] - netRounds.kills));
            netRounds.kills = match.kills[match.me];
        }

        // What the others said.
        for (int i = 0; i < match.roundEventCount; ++i)
        {
            const NetEvent &e = match.roundEvents[i];
            if (e.type == NetEventType::CsPlant && e.from != match.me &&
                (r.phase == Phase::Live || (!host && r.phase != Phase::Planted && r.phase != Phase::Over)))
            {
                const fx at[3] = {e.v[0] * 1024, e.v[1] * 1024, e.v[2] * 1024};
                detail::csPlantAt(r, at);
                out.planted = true;
                netRounds.carrier = -1;
            }
            else if (e.type == NetEventType::CsDefuse && host && r.phase == Phase::Planted)
            {
                out.defused = out.ended = true;
                endRound(r, cs, kTeamCT, Why::BombDefused);
            }
            else if (e.type == NetEventType::CsRound && !host)
                detail::csTakeRound(match, r, in, cs, e, out);
            // An order to the host's bots on the sayer's side (HlCs.hpp):
            // FOLLOW ME after their player here.
            else if (e.type == NetEventType::CsOrder && host && e.from < kNetPlayers)
                giveOrder(netTeamOf(match, e.from), static_cast<Order>(e.a),
                          static_cast<std::int16_t>(match.firstActor >= 0 ? match.firstActor + e.from : -1));
            // The bomb put down by the one with it; taken up by a terrorist
            // walking over it, alive -- the host's to say.
            else if (e.type == NetEventType::CsDrop && host && e.from == netRounds.carrier && r.phase == Phase::Live)
            {
                r.bombDropped = true;
                for (int k = 0; k < 3; ++k)
                    r.bombDrop[k] = e.v[k] * 1024;
                netRounds.carrier = -1;
            }
            else if (e.type == NetEventType::CsTake && host && r.bombDropped && r.phase == Phase::Live &&
                     e.from < kNetPlayers && netTeamOf(match, e.from) == kTeamT && netAlive(match, e.from, in.alive))
            {
                r.bombDropped = false;
                netRounds.carrier = e.from;
            }
            else if (e.type == NetEventType::CsBomb && !host)
            {
                r.bombDropped = e.a != 0;
                for (int k = 0; k < 3; ++k)
                    r.bombDrop[k] = e.v[k] * 1024;
                netRounds.takeSaid = false;
            }
        }
        match.roundEventCount = 0;

        if (host)
        {
            const RoundOut own = roundTick(r, in, cs);
            out.started |= own.started;
            out.unfrozen |= own.unfrozen;
            out.exploded |= own.exploded;
            out.blast = own.blast > out.blast ? own.blast : out.blast;
            out.defused |= own.defused;
            out.ended |= own.ended;
            out.beep |= own.beep;
            if (own.planted)
            {
                // (The host's own plant: where, for the others.)
                out.planted = true;
                netRounds.carrier = -1;
                netSay(match, netEvent(NetEventType::CsPlant, 0, r.bomb[0] >> 10, r.bomb[1] >> 10, r.bomb[2] >> 10));
            }
            // The bomb: to a terrorist at the round's start, in turn. Then
            // whoever has it as this console saw it go (botsThink: a bot or
            // this player shot down with it drops it, a terrorist over it
            // takes it up), or as another's player said (CsDrop, CsTake);
            // theirs, fallen with it, lies where they fell.
            if (r.bombMap && (out.started || netRounds.saidNumber < 0))
            {
                netRounds.carrier = netBombTo(match, r.number, in.alive);
                r.bombDropped = false;
            }
            else if (r.bombMap && r.phase == Phase::Live)
            {
                if (r.bombDropped)
                    netRounds.carrier = -1;
                else if (r.botBomb >= 0)
                {
                    const int seat = netSeatOfActor(match, r.botBomb);
                    if (seat >= 0 && seat < kNetPlayers && match.bot[seat])
                        netRounds.carrier = seat;
                }
                else if (r.hasBomb)
                    netRounds.carrier = match.me;
                const int now = netRounds.carrier;
                if (now >= 0 && now < kNetPlayers && now != match.me && !match.bot[now] && !netAlive(match, now, in.alive))
                {
                    const NetPlayer &them = netPlayer(match, now);
                    r.bombDropped = true;
                    for (int k = 0; k < 3; ++k)
                        r.bombDrop[k] = them.pos[k] * 1024 - (k == 2 ? fxFromInt(36) : 0);
                    netRounds.carrier = -1;
                }
                // (Nobody's, and lying nowhere: someone's, as at the start.)
                if (netRounds.carrier < 0 && !r.bombDropped && !r.plantedThisRound)
                    netRounds.carrier = netBombTo(match, r.number, in.alive);
            }
            if (r.phase != Phase::Freeze && r.phase != Phase::Live)
                netRounds.carrier = -1;
            r.hasBomb = r.bombMap && netRounds.carrier == match.me;
            // (A bot's, the bomb: that bot's, as alone.)
            r.botBomb = r.bombMap && netRounds.carrier >= 0 && match.bot[netRounds.carrier]
                            ? netRounds.seatActor[netRounds.carrier]
                            : -1;
            if (r.phase != Phase::Live)
                r.bombDropped = false;
            // Where it lies, said as that changes.
            {
                std::int16_t at[3];
                for (int k = 0; k < 3; ++k)
                    at[k] = static_cast<std::int16_t>(r.bombDrop[k] >> 10);
                const bool moved = r.bombDropped && (at[0] != netRounds.saidDrop[0] || at[1] != netRounds.saidDrop[1] ||
                                                     at[2] != netRounds.saidDrop[2]);
                if (r.bombDropped != netRounds.saidDropped || moved)
                {
                    netRounds.saidDropped = r.bombDropped;
                    for (int k = 0; k < 3; ++k)
                        netRounds.saidDrop[k] = at[k];
                    netSay(match, netEvent(NetEventType::CsBomb, r.bombDropped ? 1 : 0, at[0], at[1], at[2]));
                }
            }
            // How it stands, said: as it changes, and each second.
            netRounds.saidMs += in.ms;
            if (r.phase != netRounds.saidPhase || r.number != netRounds.saidNumber || netRounds.saidMs >= kNetSayMs)
            {
                netRounds.saidMs = 0;
                netRounds.saidPhase = r.phase;
                netRounds.saidNumber = r.number;
                netSay(match, netRoundEvent(r, netRounds.carrier));
            }
            return out;
        }

        // A client: the clock between the host's words, the bomb's beeps;
        // its player's plant, and defuse, said.
        r.phaseMs += in.ms;
        if (r.phase == Phase::Live)
        {
            r.liveMs += in.ms;
            r.clockMs = r.clockMs > in.ms ? r.clockMs - in.ms : 0;
        }
        if (r.phase == Phase::Planted)
        {
            r.bombMs = r.bombMs > in.ms ? r.bombMs - in.ms : 0;
            r.beepMs -= in.ms;
            if (r.beepMs <= 0 && r.bombMs > 0)
            {
                out.beep = true;
                r.beepMs = r.bombMs > 20000 ? 1500 : r.bombMs > 10000 ? 1000 : r.bombMs > 5000 ? 500 : 250;
            }
            if (in.team == kTeamCT && in.alive && in.actionHeld && nearBomb(r, in.origin) && !netRounds.defuseSaid)
            {
                r.defuseMs += in.ms;
                if (r.defuseMs >= (in.hasKit ? kDefuseKitMs : kDefuseMs))
                {
                    netRounds.defuseSaid = true;
                    netSay(match, netEvent(NetEventType::CsDefuse));
                }
            }
            else if (!netRounds.defuseSaid)
                r.defuseMs = 0;
        }
        // The bomb lying, walked over by a terrorist alive: said.
        if (r.phase == Phase::Live && r.bombDropped && in.team == kTeamT && in.alive && in.origin != nullptr &&
            !netRounds.takeSaid)
        {
            const fx feet[3] = {in.origin[0], in.origin[1], in.origin[2] - fxFromInt(36)};
            const fx d[3] = {feet[0] - r.bombDrop[0], feet[1] - r.bombDrop[1], 0};
            const fx up = feet[2] - r.bombDrop[2];
            if (fxLength(d) < fxFromInt(48) && up < fxFromInt(64) && up > -fxFromInt(64))
            {
                netRounds.takeSaid = true;
                netSay(match, netEvent(NetEventType::CsTake));
            }
        }
        if (r.phase == Phase::Live && r.hasBomb && in.team == kTeamT && in.alive && in.inBombSite && in.actionHeld &&
            in.origin != nullptr)
        {
            r.plantMs += in.ms;
            if (r.plantMs >= kPlantMs)
            {
                detail::csPlantAt(r, in.origin);
                out.planted = true;
                netRounds.carrier = -1;
                netSay(match, netEvent(NetEventType::CsPlant, 0, r.bomb[0] >> 10, r.bomb[1] >> 10, r.bomb[2] >> 10));
            }
        }
        else
            r.plantMs = 0;
        return out;
    }
} // namespace hldata::cs
