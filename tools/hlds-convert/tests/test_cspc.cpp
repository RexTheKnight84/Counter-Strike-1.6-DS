// Counter-Strike 1.6 as a PC plays it (shared/HlCsPc.hpp): speeds by
// weapon, damage over distance, by where it hits, through armour.

#include <cstdio>

#include "HlCs.hpp"
#include "HlCsPc.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldata::cs;
using namespace hldstest;

int main()
{
    // Every gun the buy menu sells has its PC numbers, and the other way.
    for (const CsGun &g : kGuns)
        check(pc::gunPc(g.item) != nullptr, g.item);
    check(pc::kGunPcCount == static_cast<int>(sizeof(kGuns) / sizeof(kGuns[0])), "one PC row for each gun");

    // Speeds.
    check(pc::maxSpeed(nullptr, false, false, false) == 250, "the knife runs at 250");
    check(pc::maxSpeed(pc::gunPc("AWP"), false, false, false) == 210, "the AWP at 210");
    check(pc::maxSpeed(pc::gunPc("AWP"), true, false, false) == 150, "... 150 through its sight");
    check(pc::maxSpeed(pc::gunPc("AK-47"), false, false, false) == 221, "the AK-47 at 221");
    check(pc::maxSpeed(pc::gunPc("SCOUT"), false, false, false) == 260, "the Scout at 260, the fastest");
    check(pc::maxSpeed(pc::gunPc("AUG"), true, false, false) == 240, "a sight that does not slow does not");
    check(pc::maxSpeed(nullptr, false, true, false) == 130, "walking: 52 hundredths");

    // Damage over distance.
    const pc::GunPc *ak = pc::gunPc("AK-47");
    check(pc::damageAtRange(36, ak->range, 0) == 36, "point blank: all of it");
    check(pc::damageAtRange(36, ak->range, 500) == 35, "500 units: 36 * 0.98");
    check(pc::damageAtRange(100, ak->range, 1000) == 96, "1000 units: 0.98 squared");
    const pc::GunPc *glock = pc::gunPc("GLOCK 18");
    check(pc::damageAtRange(100, glock->range, 1000) == 56, "a Glock at 1000: 0.75 squared");
    check(pc::damageAtRange(100, glock->range, 250) == 88, "... at 250, halfway to 0.75 (on the line)");
    check(pc::damageAtRange(20, pc::gunPc("M3")->range, 2000) == 20, "a shotgun's pellet is left alone");

    // Where it hits.
    check(pc::shot(36, 155, ak, 0, pc::kHitHead, 0, false).health == 144, "an AK in the bare head: 144");
    check(pc::shot(36, 155, ak, 0, pc::kHitStomach, 0, false).health == 45, "... in the stomach: 45");
    check(pc::shot(36, 155, ak, 0, pc::kHitLeftLeg, 0, false).health == 27, "... in the leg: 27");

    // Armour.
    {
        const pc::Hurt h = pc::shot(36, 155, ak, 0, pc::kHitHead, 100, true);
        check(h.health == 111 && h.armour == 16, "an AK in a helmet: 111 to the head, 16 off the armour");
        const pc::Hurt n = pc::shot(36, 155, ak, 0, pc::kHitHead, 100, false);
        check(n.health == 144 && n.armour == 0, "... a vest alone does not cover the head");
        const pc::Hurt l = pc::shot(36, 155, ak, 0, pc::kHitLeftLeg, 100, true);
        check(l.health == 27 && l.armour == 0, "... nor does anything cover the legs");
        const pc::Hurt c = pc::shot(25, 105, glock, 0, pc::kHitChest, 100, true);
        check(c.health == 13 && c.armour == 6, "a Glock in a vest: 13 through, 6 off it");
        const pc::Hurt thin = pc::armourHit(100, 5, 100);
        check(thin.armour == 5 && thin.health == 90, "armour too thin gives out, and the rest goes through");
    }

    check(pc::damageThrough(100, 1) == 75 && pc::damageThrough(100, 2) == 56, "each wall takes a quarter");

    if (g_failures == 0)
        std::printf("test_cspc: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
