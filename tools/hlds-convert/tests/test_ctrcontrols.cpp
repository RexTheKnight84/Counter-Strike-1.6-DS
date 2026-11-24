// The 3DS's controls (shared/HlCtrControls.hpp): the DS's layouts, played
// with the circle pad, a larger touch screen, and the New 3DS's ZL, ZR and
// C-stick.

#include <cstdio>

#include "HlCtrControls.hpp"
#include "HlCs.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

int main()
{
    // The buttons the two share are the same bits.
    check(ctr::toDs({kButtonA | kButtonL | kButtonStart}).held == (kButtonA | kButtonL | kButtonStart),
          "the DS's twelve buttons go through as they are");
    check(ctr::toDs({ctr::kKeyZL | ctr::kKeyZR | ctr::kKeyCStickUp}).held == 0,
          "... and the 3DS's own do not reach the DS's controls");

    // The circle pad: eight ways, at rest near the middle.
    check(ctr::circleAsPad(0, 0) == 0 && ctr::circleAsPad(30, 20) == 0, "the circle pad at rest holds nothing");
    check(ctr::circleAsPad(0, 150) == kButtonUp, "pushed up, it is up");
    check(ctr::circleAsPad(-150, 0) == kButtonLeft, "pushed left, it is left");
    check(ctr::circleAsPad(100, 100) == (kButtonUp | kButtonRight), "on the diagonal, both");
    check(ctr::circleAsPad(140, 30) == kButtonRight, "near enough right, only right");

    // The touch screen: the DS's layout a quarter larger.
    check(ctr::touchToDs(319) == 255 && ctr::touchToDs(239) == 191, "the corner is the DS's corner");
    {
        ctr::In in;
        in.touching = true;
        in.touchX = 400; // (a reading off the edge)
        in.touchY = 120;
        const ControlsIn ds = ctr::toDs(in);
        check(ds.touchX == 255 && ds.touchY == 96, "a touch off the edge is at the edge");
        check(ds.fineX / 16 == ds.touchX && ds.fineY / 16 == ds.touchY, "the finer reading agrees with the pixel");
        check(ds.pressure == 0, "and says nothing of how hard");
    }
    {
        const TouchLayout at = touchLayout(false);
        const int reloadX = ctr::dsToTouch(at.reload.x + at.reload.w / 2);
        const int reloadY = ctr::dsToTouch(at.reload.y + at.reload.h / 2);
        check(touchHit(at, ctr::touchToDs(reloadX), ctr::touchToDs(reloadY)) == kTouchReload,
              "RELOAD drawn on the 3DS is where the DS's layout has it");
    }

    // Every layout, moved by the circle pad as by the D-pad.
    for (std::uint8_t scheme : {kControlsClassic, kControlsModern})
    {
        ControlSettings set;
        set.scheme = scheme;
        ctr::Settings ctrSet;
        ControlsState state;
        std::uint32_t last = 0;
        ctr::In in;
        in.circleY = 156;
        in.blank = 1;
        const ControlsOut out = ctr::step(set, ctrSet, in, state, last);
        check(out.moveForward == 1 && !out.walk, "the circle pad pushed all the way runs forward");
        in.circleY = 80;
        in.blank = 2;
        const ControlsOut half = ctr::step(set, ctrSet, in, state, last);
        check(half.moveForward == 1 && half.walk, "... part of the way, walks");
        ctrSet.analogWalk = false;
        in.blank = 3;
        check(!ctr::step(set, ctrSet, in, state, last).walk, "... unless the player would rather run");
    }
    {
        // The D-pad held as well: a key, not a tilt; no walk.
        ControlSettings set;
        ctr::Settings ctrSet;
        ControlsState state;
        std::uint32_t last = 0;
        ctr::In in;
        in.held = kButtonUp;
        in.circleY = 80;
        in.blank = 1;
        check(!ctr::step(set, ctrSet, in, state, last).walk, "the D-pad runs, whatever the circle pad says");
    }

    // ZL, ZR and the C-stick.
    {
        ControlSettings set;
        ctr::Settings ctrSet;
        ControlsState state;
        std::uint32_t last = 0;
        ctr::In in;
        in.held = ctr::kKeyZR;
        in.blank = 1;
        ControlsOut out = ctr::step(set, ctrSet, in, state, last);
        check(out.jump && !out.duck, "ZR jumps");
        in.held = ctr::kKeyZL;
        in.blank = 2;
        out = ctr::step(set, ctrSet, in, state, last);
        check(out.duck && !out.jump, "ZL crouches, held");
        ctrSet.zr = kActionReload;
        in.held = ctr::kKeyZR;
        in.blank = 3;
        check(ctr::step(set, ctrSet, in, state, last).reloadPressed, "ZR given the reload reloads");
        in.blank = 4;
        check(!ctr::step(set, ctrSet, in, state, last).reloadPressed, "... once, however long it is held");

        in.held = 0;
        in.cstickX = 146;
        in.cstickY = 146;
        in.blank = 5;
        out = ctr::step(set, ctrSet, in, state, last);
        check(out.lookFineX == 64 && out.lookFineY == -64, "the C-stick over: four pixels a frame, up looks up");
        in.cstickX = 10;
        in.cstickY = -10;
        in.blank = 6;
        out = ctr::step(set, ctrSet, in, state, last);
        check(out.lookFineX == 0 && out.lookFineY == 0, "the C-stick at rest turns nothing");
    }

    if (g_failures == 0)
        std::printf("test_ctrcontrols: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
