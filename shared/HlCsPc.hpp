#pragma once

// Counter-Strike 1.6 as a PC plays it, where the DS's Counter-Strike
// (HlCs.hpp) has had to stand Half-Life in for it, for a console with the
// room and the speed to do it the PC's way (the 3DS: ctr/).
//
// - How fast each weapon lets its carrier run, and with its sight up.
// - A round's damage over distance: the gun's range modifier, raised to
//   the distance in 500-unit steps.
// - Where it hits: the head four times, the stomach a quarter more, the
//   legs three quarters.
// - Armour: what a vest (and a helmet, for the head) takes of a round, by
//   the gun's armour ratio (HlCs.hpp's armorPierce), and what it costs the
//   armour; the legs are never covered.
// - How many surfaces a round may go through.
// - The movement's own constants (sv_accelerate, sv_friction,
//   sv_stopspeed, the walk key's fraction), which are not Half-Life's.
//
// The numbers are the game's defaults, as observed in play and in its
// published weapon tables; tools/ref is where they are to be checked
// against the real game, as the rest of the port is.
//
// Integers only, in the hot path's terms: hundredths, 65536ths. No runtime
// division but by constants. std-only and inline: the host tests run it
// (tests/test_cspc.cpp).

#include <cstdint>
#include <cstring>

namespace hldata::cs::pc
{
    // ------------------------------------------------------------- moving

    /// sv_maxspeed: no one goes faster on the ground, whatever they hold.
    inline constexpr int kServerMaxSpeed = 320;
    inline constexpr int kAccelerate = 5;     ///< sv_accelerate (Half-Life's is 10)
    inline constexpr int kAirAccelerate = 10; ///< sv_airaccelerate
    inline constexpr int kFriction = 4;       ///< sv_friction
    inline constexpr int kStopSpeed = 75;     ///< sv_stopspeed (Half-Life's is 100)
    /// The walk key: 52 hundredths of the speed (cl_movespeedkey). Silent:
    /// no footsteps under it.
    inline constexpr int kWalkHundredths = 52;
    /// Crouched: a third of it.
    inline constexpr int kDuckHundredths = 33;
    /// The knife's, the grenades', the bomb's, an empty hand's.
    inline constexpr int kDefaultSpeed = 250;

    /// What a gun does besides its HlCs.hpp numbers.
    struct GunPc
    {
        const char *item;  ///< HlCs.hpp's kItems name
        std::int16_t speed;       ///< the most its carrier runs
        std::int16_t scopedSpeed; ///< ... with its sight up (0: it has none)
        std::uint16_t range;      ///< range modifier, in 65536ths
        std::uint8_t penetration; ///< surfaces a round may go through, and on
        std::uint16_t penetrationReach; ///< how far a round still kills, after a wall (units)
    };

    inline constexpr std::uint16_t rangeOf(int tenThousandths) { return static_cast<std::uint16_t>(tenThousandths * 65536 / 10000); }

    inline constexpr GunPc kGunsPc[] = {
        {"GLOCK 18", 250, 0, rangeOf(7500), 1, 4096},
        {"USP", 250, 0, rangeOf(7900), 1, 4096},
        {"P228", 250, 0, rangeOf(8000), 1, 4096},
        {"DESERT EAGLE", 250, 0, rangeOf(8100), 2, 4096},
        {"FIVE-SEVEN", 250, 0, rangeOf(8850), 1, 4096},
        {"DUAL ELITES", 250, 0, rangeOf(7500), 1, 8192},
        {"M3", 230, 0, 0, 0, 3000},
        {"XM1014", 240, 0, 0, 0, 3048},
        {"MAC-10", 250, 0, rangeOf(8200), 1, 8192},
        {"TMP", 250, 0, rangeOf(8500), 1, 8192},
        {"MP5", 250, 0, rangeOf(8400), 1, 8192},
        {"UMP45", 250, 0, rangeOf(8200), 1, 8192},
        {"P90", 245, 0, rangeOf(8850), 1, 8192},
        {"GALIL", 240, 0, rangeOf(9800), 2, 8192},
        {"FAMAS", 240, 0, rangeOf(9600), 2, 8192},
        {"AK-47", 221, 0, rangeOf(9800), 2, 8192},
        {"M4A1", 230, 0, rangeOf(9700), 2, 8192},
        {"SCOUT", 260, 220, rangeOf(9800), 3, 8192},
        {"SG 552", 235, 200, rangeOf(9550), 2, 8192},
        {"AUG", 240, 0, rangeOf(9600), 2, 8192},
        {"AWP", 210, 150, rangeOf(9900), 3, 8192},
        {"G3SG1", 210, 150, rangeOf(9800), 3, 8192},
        {"SG 550", 210, 150, rangeOf(9800), 2, 8192},
        {"M249", 220, 0, rangeOf(9700), 2, 8192},
    };
    inline constexpr int kGunPcCount = static_cast<int>(sizeof(kGunsPc) / sizeof(kGunsPc[0]));

    /// The gun called `item` (HlCs.hpp's name), or none.
    inline const GunPc *gunPc(const char *item)
    {
        if (item == nullptr)
            return nullptr;
        for (const GunPc &g : kGunsPc)
        {
            if (std::strcmp(g.item, item) == 0)
                return &g;
        }
        return nullptr;
    }

    /// How fast a player may run: by what is in hand (none: the knife's),
    /// its sight, the walk key, the crouch. Never past sv_maxspeed.
    inline int maxSpeed(const GunPc *gun, bool scoped, bool walking, bool ducked)
    {
        int speed = gun != nullptr ? (scoped && gun->scopedSpeed > 0 ? gun->scopedSpeed : gun->speed) : kDefaultSpeed;
        if (walking)
            speed = speed * kWalkHundredths / 100;
        if (ducked)
            speed = speed * kDuckHundredths / 100;
        return speed > kServerMaxSpeed ? kServerMaxSpeed : speed;
    }

    // ------------------------------------------------------------- damage

    enum HitGroup : std::uint8_t
    {
        kHitGeneric,
        kHitHead,
        kHitChest,
        kHitStomach,
        kHitLeftArm,
        kHitRightArm,
        kHitLeftLeg,
        kHitRightLeg,
    };

    /// What a hit there does, in hundredths of the round's damage.
    inline constexpr int hitHundredths(std::uint8_t group)
    {
        return group == kHitHead      ? 400
               : group == kHitStomach ? 125
               : (group == kHitLeftLeg || group == kHitRightLeg) ? 75
                                                                 : 100;
    }

    /// The round's damage after `distance` units: the range modifier per
    /// 500, its whole steps multiplied out and the part step between them
    /// taken on the straight line (the modifier is near 1, so the curve and
    /// the line part by under a hundredth). Shotguns (range 0) are left as
    /// they are: their pellets fall off otherwise.
    inline int damageAtRange(int damage, std::uint16_t range, int distance)
    {
        if (range == 0 || distance <= 0)
            return damage;
        std::uint32_t scale = 65536; // in 65536ths
        int d = distance;
        while (d >= 500)
        {
            scale = (scale * range) >> 16;
            d -= 500;
        }
        // The part step: scale * (1 - (1 - range) * d / 500).
        const std::uint32_t lose = static_cast<std::uint32_t>(65536 - range) * static_cast<std::uint32_t>(d) / 500;
        scale = static_cast<std::uint32_t>((static_cast<std::uint64_t>(scale) * (65536 - lose)) >> 16);
        return static_cast<int>((static_cast<std::uint64_t>(damage) * scale + 32768) >> 16);
    }

    /// Whether armour covers `group`: the body and the arms with a vest,
    /// the head only with a helmet as well, the legs never.
    inline constexpr bool armourCovers(std::uint8_t group, bool helmet)
    {
        if (group == kHitHead)
            return helmet;
        return group != kHitLeftLeg && group != kHitRightLeg;
    }

    struct Hurt
    {
        int health = 0; ///< what the body takes
        int armour = 0; ///< what the armour loses
    };

    /// A hit of `damage` (already for its place and its distance) on armour
    /// `armour`: the body takes the gun's ratio of it -- half, by the gun's
    /// armour-ratio modifier (`pierce`, hundredths: HlCs.hpp's armorPierce)
    /// -- and the armour half of the rest. Armour too thin for that gives
    /// out, and what it could not take goes to the body.
    inline Hurt armourHit(int damage, int armour, int pierce)
    {
        Hurt hurt;
        if (armour <= 0)
        {
            hurt.health = damage;
            return hurt;
        }
        // ratio = 0.5 * pierce/100, in 200ths of pierce.
        int body = damage * pierce / 200;
        if (body > damage)
            body = damage;
        int taken = (damage - body) / 2;
        if (taken > armour)
        {
            taken = armour;
            body = damage - armour * 2;
        }
        hurt.health = body;
        hurt.armour = taken;
        return hurt;
    }

    /// Everything at once: a round of `damage` from `gun` (HlCs.hpp's
    /// numbers: damage and armorPierce) over `distance`, at `group`, on a
    /// target with `armour` and a helmet or not.
    inline Hurt shot(int damage, int pierce, const GunPc *gun, int distance, std::uint8_t group, int armour,
                     bool helmet)
    {
        int d = gun != nullptr ? damageAtRange(damage, gun->range, distance) : damage;
        d = d * hitHundredths(group) / 100;
        if (!armourCovers(group, helmet))
            armour = 0;
        return armourHit(d, armour, pierce);
    }

    /// Through a wall: each surface gone through, the damage keeps this
    /// much (a rifle's round its 65536ths). The game takes more off a
    /// thicker or harder surface; this is its plain-wall case.
    inline int damageThrough(int damage, int surfacesGone)
    {
        for (int n = 0; n < surfacesGone; ++n)
            damage = damage * 3 / 4;
        return damage;
    }
} // namespace hldata::cs::pc
