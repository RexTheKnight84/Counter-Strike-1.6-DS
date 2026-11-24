#pragma once

// The 3DS's controls (the native 3DS program, ctr/), played by the DS's
// layouts: what HlControls.hpp says the buttons and the stylus mean is what
// they mean here, every layout, either hand. This only says what the 3DS
// has that the DS has not, and how it is put to the DS's terms.
//
// THE BUTTONS. libctru's key bits for A B Select Start, the D-pad, R L X Y
// are the DS key register's, bit for bit; they go through as they are.
//
// THE CIRCLE PAD is the pad: its tilt holds the D-pad's directions (eight
// ways, as WASD would), so every layout moves with it as with the D-pad. As
// Counter-Strike on a PC runs unless the walk key is held, a full tilt runs;
// a partial one walks, when the player asks for it (analogWalk).
//
// THE TOUCH SCREEN is 320 by 240, the DS's 256 by 192 made a quarter
// larger each way: a touch is scaled by four fifths onto the DS's layout
// (HlTouchLayout.hpp), and drawn there a quarter larger, so the two agree.
// The 3DS reads it finer than its pixels; it is passed on in sixteenths of
// the DS's pixels, as a DS's calibrated reading is. It cannot say how hard
// it is pressed: none of the firmness is done, as on a DSi in its own mode.
//
// THE NEW 3DS (and an old one with the Circle Pad Pro) has ZL, ZR and the
// C-stick. The C-stick looks, as a mouse does; ZL and ZR are an action each
// (by default the crouch and the jump, the PC's Ctrl and Space). An old 3DS
// without them loses nothing the DS layouts had.
//
// std-only and inline: the host tests run it (tests/test_ctrcontrols.cpp).

#include <cstdint>

#include "HlControls.hpp"

namespace hldata::ctr
{
    // libctru's KEY_* bits beyond the DS's twelve.
    inline constexpr std::uint32_t kKeyZL = 1u << 14;
    inline constexpr std::uint32_t kKeyZR = 1u << 15;
    inline constexpr std::uint32_t kKeyTouch = 1u << 20;
    inline constexpr std::uint32_t kKeyCStickRight = 1u << 24;
    inline constexpr std::uint32_t kKeyCStickLeft = 1u << 25;
    inline constexpr std::uint32_t kKeyCStickUp = 1u << 26;
    inline constexpr std::uint32_t kKeyCStickDown = 1u << 27;
    inline constexpr std::uint32_t kKeyCPadRight = 1u << 28;
    inline constexpr std::uint32_t kKeyCPadLeft = 1u << 29;
    inline constexpr std::uint32_t kKeyCPadUp = 1u << 30;
    inline constexpr std::uint32_t kKeyCPadDown = 1u << 31;
    /// The twelve the DS has.
    inline constexpr std::uint32_t kKeysDs = 0xFFFu;

    inline constexpr int kTopWidth = 400, kTopHeight = 240;
    inline constexpr int kBottomWidth = 320, kBottomHeight = 240;

    /// The circle pad's reach (hidCircleRead: about this far each way), the
    /// tilt below which it is at rest, and the one below which a tilt walks.
    inline constexpr int kCircleMost = 156;
    inline constexpr int kCircleDead = 40;
    inline constexpr int kCircleWalk = 110;
    /// The C-stick's (irrstCstickRead), and its rest.
    inline constexpr int kCStickMost = 146;
    inline constexpr int kCStickDead = 16;
    /// The C-stick held right over: this many DS pixels of stylus travel a
    /// sixtieth, at its speed's default -- about 165 degrees a second.
    inline constexpr int kCStickTurnPixels = 4;

    /// What ZL and ZR may be: an action of HlControls.hpp's, or none.
    inline constexpr std::uint8_t kNoAction = 0xFF;

    struct Settings
    {
        bool analogWalk = true; ///< a partial tilt of the circle pad walks
        std::uint8_t cstickSpeed = kAimSpeedDefault; ///< 1..kAimSpeedMost
        bool cstickInvertY = false;
        std::uint8_t zl = kActionDuck;
        std::uint8_t zr = kActionJump;
    };

    /// What the 3DS has this frame.
    struct In
    {
        std::uint32_t held = 0; ///< hidKeysHeld()
        int circleX = 0, circleY = 0; ///< hidCircleRead(): up is +
        int cstickX = 0, cstickY = 0; ///< irrstCstickRead(): up is +
        bool touching = false;
        int touchX = 0, touchY = 0; ///< hidTouchRead(), on the 320 by 240
        std::uint32_t blank = 0;
    };

    /// The circle pad's tilt as the D-pad's bits: eight ways, each within
    /// 22.5 degrees of its own (tan 22.5 is near enough two fifths).
    inline std::uint32_t circleAsPad(int x, int y)
    {
        if (x * x + y * y < kCircleDead * kCircleDead)
            return 0;
        const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
        std::uint32_t bits = 0;
        if (ax * 5 > ay * 2)
            bits |= x > 0 ? kButtonRight : kButtonLeft;
        if (ay * 5 > ax * 2)
            bits |= y > 0 ? kButtonUp : kButtonDown;
        return bits;
    }

    /// A point on the 3DS's touch screen, on the DS's (its pixels).
    inline constexpr int touchToDs(int v) { return v * 4 / 5; }
    /// ... in sixteenths of the DS's pixels.
    inline constexpr int touchToDsFine(int v) { return v * 64 / 5; }
    /// A DS layout's length, drawn on the 3DS's screen.
    inline constexpr int dsToTouch(int v) { return v * 5 / 4; }

    /// The frame put to the DS's terms, for controlsStep().
    inline ControlsIn toDs(const In &in)
    {
        ControlsIn ds;
        ds.held = (in.held & kKeysDs) | circleAsPad(in.circleX, in.circleY);
        ds.blank = in.blank;
        if (in.touching)
        {
            const int x = in.touchX < 0 ? 0 : (in.touchX >= kBottomWidth ? kBottomWidth - 1 : in.touchX);
            const int y = in.touchY < 0 ? 0 : (in.touchY >= kBottomHeight ? kBottomHeight - 1 : in.touchY);
            ds.touching = true;
            ds.touchX = touchToDs(x);
            ds.touchY = touchToDs(y);
            ds.fineX = touchToDsFine(x);
            ds.fineY = touchToDsFine(y);
        }
        return ds;
    }

    /// What the 3DS adds to the DS's answer: the walk by the tilt, the
    /// C-stick's look, ZL's and ZR's actions. `lastHeld`: the 3DS's keys last
    /// frame (for an action's press).
    inline void finish(const Settings &set, const In &in, std::uint32_t lastHeld, ControlsOut &out)
    {
        // The walk: moving by the circle pad alone, tilted part of the way.
        const int r2 = in.circleX * in.circleX + in.circleY * in.circleY;
        const bool padDigital = (in.held & (kButtonUp | kButtonDown | kButtonLeft | kButtonRight)) != 0;
        if (set.analogWalk && !padDigital && r2 >= kCircleDead * kCircleDead && r2 < kCircleWalk * kCircleWalk &&
            (out.moveForward != 0 || out.moveSide != 0))
            out.walk = true;

        // The C-stick: as a stylus dragged, so the aim speed and the view's
        // width scale it as they do the stylus.
        auto axis = [](int v) {
            if (v > -kCStickDead && v < kCStickDead)
                return 0;
            v = v > kCStickMost ? kCStickMost : (v < -kCStickMost ? -kCStickMost : v);
            return v;
        };
        const int cx = axis(in.cstickX), cy = axis(set.cstickInvertY ? -in.cstickY : in.cstickY);
        if (cx != 0 || cy != 0)
        {
            const int speed = set.cstickSpeed < 1 ? 1 : (set.cstickSpeed > kAimSpeedMost ? kAimSpeedMost : set.cstickSpeed);
            const int fineX = cx * kCStickTurnPixels * 16 * speed / (kCStickMost * kAimSpeedDefault);
            const int fineY = -cy * kCStickTurnPixels * 16 * speed / (kCStickMost * kAimSpeedDefault);
            out.lookFineX += fineX;
            out.lookFineY += fineY;
            out.lookDx += fineX / 16;
            out.lookDy += fineY / 16;
        }

        // ZL and ZR.
        const std::uint32_t pressed = in.held & ~lastHeld;
        auto act = [&](std::uint8_t action, std::uint32_t key) {
            const bool on = (in.held & key) != 0, now = (pressed & key) != 0;
            switch (action)
            {
            case kActionFire: out.attack |= on; break;
            case kActionAltFire: out.second |= on; break;
            case kActionJump: out.jump |= on; break;
            case kActionDuck: out.duck |= on; break;
            case kActionWalk: out.walk |= on; break;
            case kActionReload: out.reloadPressed |= now; break;
            case kActionUse:
                out.usePressed |= now;
                out.useHeld |= on;
                break;
            case kActionLamp: out.lampPressed |= now; break;
            case kActionNextWeapon:
                if (now)
                    out.weaponStep = 1;
                break;
            case kActionPrevWeapon:
                if (now)
                    out.weaponStep = -1;
                break;
            default: break;
            }
        };
        act(set.zl, kKeyZL);
        act(set.zr, kKeyZR);
    }

    /// One frame: the DS's controls, played on a 3DS.
    inline ControlsOut step(const ControlSettings &dsSet, const Settings &set, const In &in, ControlsState &state,
                            std::uint32_t &lastHeld)
    {
        ControlsOut out = controlsStep(dsSet, toDs(in), state);
        finish(set, in, lastHeld, out);
        lastHeld = in.held;
        return out;
    }
} // namespace hldata::ctr
