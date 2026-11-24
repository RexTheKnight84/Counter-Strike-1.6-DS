// The native 3DS program, as far as it has come: a map from the DS port's
// cooked data on the SD card, drawn by the 3DS's GPU (World.hpp), looked
// round and flown through with the DS's control layouts as the 3DS plays
// them (HlCtrControls.hpp), and the DS's touch layout on the bottom screen.
//
// Select held: left and right on the D-pad change the map, R the control
// layout, L the hand. Select and Start together: out. ctr/README.md says what
// comes next.

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>

#include "HlControls.hpp"
#include "HlCs.hpp"
#include "HlCsPc.hpp"
#include "HlCtrControls.hpp"
#include "HlTouchLayout.hpp"
#include "World.hpp"

namespace
{
    // UI colours (plain: the port's own frames, no game art).
    u32 kBack, kFrame, kFrameLit, kText, kShade;

    C2D_TextBuf textBuf;

    void text(float x, float y, float size, const char *s)
    {
        C2D_Text t;
        C2D_TextParse(&t, textBuf, s);
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, size, size, kText);
    }

    /// A DS layout's rectangle, on the 3DS's bottom screen.
    void frame(const hldata::TouchRect &r, bool lit, const char *name)
    {
        if (r.w <= 0)
            return;
        using hldata::ctr::dsToTouch;
        const float x = dsToTouch(r.x), y = dsToTouch(r.y), w = dsToTouch(r.w), h = dsToTouch(r.h);
        C2D_DrawRectSolid(x, y, 0.4f, w, h, lit ? kFrameLit : kFrame);
        C2D_DrawRectSolid(x + 1, y + 1, 0.45f, w - 2, h - 2, kBack);
        if (name)
            text(x + 4, y + h / 2 - 7, 0.45f, name);
    }

    constexpr const char *kMaps[] = {"de_dust2", "de_inferno", "de_nuke"};
    constexpr int kMapCount = sizeof(kMaps) / sizeof(kMaps[0]);
    constexpr const char *kDataDir = "sdmc:/_nds/hl_nds/data/cstrike/";

    struct Camera
    {
        float pos[3] = {0, 0, 0};
        float yaw = 0, pitch = 0; ///< degrees: yaw round from +x, pitch up
    };
} // namespace

int main()
{
    osSetSpeedupEnable(true); // (a New 3DS's clock and cache; nothing on an old one)
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    // The C-stick and ZL/ZR: a New 3DS's, or the Circle Pad Pro's on an old.
    const bool irrst = R_SUCCEEDED(irrstInit());
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuf = C2D_TextBufNew(4096);
    kBack = C2D_Color32(18, 20, 18, 255);
    kFrame = C2D_Color32(120, 128, 110, 255);
    kFrameLit = C2D_Color32(255, 170, 40, 255);
    kText = C2D_Color32(230, 230, 220, 255);
    kShade = C2D_Color32(0, 0, 0, 140);

    hldata::ControlSettings dsSet;
    hldata::ctr::Settings ctrSet;
    hldata::ControlsState state;
    std::uint32_t lastHeld = 0;
    std::uint32_t blank = 0;
    int schemeAt = 0;

    ctrport::World world;
    Camera camera;
    int mapAt = 0;
    bool wantMap = true;
    const char *mapError = nullptr;

    // The view: the PC's 90 degrees across a 4:3 screen, kept in height on
    // the wider top screen (as Counter-Strike on a wide screen sees more to
    // the sides, not less above and below).
    const float fovY = 2.0f * std::atan(std::tan(45.0f * static_cast<float>(M_PI) / 180.0f) * 3.0f / 4.0f);
    C3D_Mtx projection;
    Mtx_PerspTilt(&projection, fovY, C3D_AspectRatioTop, 4.0f, 8192.0f, false);

    while (aptMainLoop())
    {
        if (wantMap)
        {
            wantMap = false;
            char mapPath[128], commonPath[128];
            std::snprintf(mapPath, sizeof mapPath, "%smaps/%s.npac", kDataDir, kMaps[mapAt]);
            std::snprintf(commonPath, sizeof commonPath, "%scommon.npac", kDataDir);
            mapError = nullptr;
            if (world.load(mapPath, commonPath, mapError))
            {
                float yaw = 0;
                if (world.spawn(hldata::cs::kTeamCT, camera.pos, yaw))
                    camera.pos[2] += 28.0f; // (the eye: a standing player's view offset)
                camera.yaw = yaw;
                camera.pitch = 0;
            }
        }

        hidScanInput();
        if (irrst)
            irrstScanInput();
        hldata::ctr::In in;
        in.held = hidKeysHeld();
        circlePosition circle;
        hidCircleRead(&circle);
        in.circleX = circle.dx;
        in.circleY = circle.dy;
        if (irrst)
        {
            circlePosition c;
            irrstCstickRead(&c);
            in.cstickX = c.dx;
            in.cstickY = c.dy;
            in.held |= irrstKeysHeld(); // (ZL and ZR, from the Circle Pad Pro)
        }
        if (in.held & KEY_TOUCH)
        {
            touchPosition touch;
            hidTouchRead(&touch);
            in.touching = true;
            in.touchX = touch.px;
            in.touchY = touch.py;
        }
        in.blank = ++blank;

        // The viewer's own: Select held is its shift, and nothing of the
        // game's sees what is pressed under it.
        const std::uint32_t down = hidKeysDown();
        if ((in.held & KEY_SELECT) && (in.held & KEY_START))
            break;
        if (in.held & KEY_SELECT)
        {
            if (down & (KEY_DRIGHT | KEY_DLEFT))
            {
                mapAt = (mapAt + ((down & KEY_DRIGHT) ? 1 : kMapCount - 1)) % kMapCount;
                wantMap = true;
            }
            if (down & KEY_R)
            {
                schemeAt = (schemeAt + 1) % hldata::kControlsSchemes;
                dsSet.scheme = hldata::kControlsOrder[schemeAt];
            }
            if (down & KEY_L)
                dsSet.leftHanded = !dsSet.leftHanded;
            in.held &= ~(KEY_DRIGHT | KEY_DLEFT | KEY_L | KEY_R);
        }

        const hldata::ControlsOut out = hldata::ctr::step(dsSet, ctrSet, in, state, lastHeld);

        // Fly: the look as the DS's Player turns it (HlControls.hpp's
        // turnOf: 65536ths of a turn), the moves at the knife's speed (the
        // PC's 250, and its walk), jump and crouch up and down.
        camera.yaw -= hldata::turnOf(out.lookFineX, hldata::kAimSpeedDefault) * (360.0f / 65536.0f);
        camera.pitch -= hldata::turnOf(out.lookFineY, hldata::kAimSpeedDefault) * (360.0f / 65536.0f);
        camera.pitch = camera.pitch > 89.0f ? 89.0f : (camera.pitch < -89.0f ? -89.0f : camera.pitch);
        const float speed = hldata::cs::pc::maxSpeed(nullptr, false, out.walk, false) / 60.0f;
        const float yawR = camera.yaw * static_cast<float>(M_PI) / 180.0f;
        const float pitchR = camera.pitch * static_cast<float>(M_PI) / 180.0f;
        const float fwd[3] = {std::cos(yawR) * std::cos(pitchR), std::sin(yawR) * std::cos(pitchR), std::sin(pitchR)};
        const float right[3] = {std::sin(yawR), -std::cos(yawR), 0};
        for (int k = 0; k < 3; ++k)
            camera.pos[k] += (fwd[k] * out.moveForward + right[k] * out.moveSide) * speed;
        camera.pos[2] += ((out.jump ? 1 : 0) - (out.duck ? 1 : 0)) * speed;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(textBuf);

        // The top screen: the world, and over it what the controls make of it.
        C2D_TargetClear(top, C2D_Color32(90, 110, 140, 255));
        C2D_SceneBegin(top);
        if (world.loaded())
        {
            C3D_Mtx view;
            const C3D_FVec eye = FVec3_New(camera.pos[0], camera.pos[1], camera.pos[2]);
            const C3D_FVec at = FVec3_New(camera.pos[0] + fwd[0], camera.pos[1] + fwd[1], camera.pos[2] + fwd[2]);
            Mtx_LookAt(&view, eye, at, FVec3_New(0, 0, 1), false);
            world.draw(projection, view);
            C2D_Prepare(); // (citro2d's state back, for the text over it)
        }
        char line[160];
        C2D_DrawRectSolid(0, 0, 0.6f, 400, 20, kShade);
        if (world.loaded())
            std::snprintf(line, sizeof line, "%s  %d tris  %d draws  %d tex  %.0f %.0f %.0f", kMaps[mapAt],
                          world.triangles(), world.batches(), world.textures(), camera.pos[0], camera.pos[1],
                          camera.pos[2]);
        else
            std::snprintf(line, sizeof line, "%s: %s", kMaps[mapAt], mapError ? mapError : "loading");
        text(4, 2, 0.45f, line);

        // The bottom screen: the DS's touch layout, a quarter larger.
        C2D_TargetClear(bottom, kBack);
        C2D_SceneBegin(bottom);
        const hldata::TouchLayout at = hldata::touchLayout(dsSet.leftHanded, false, dsSet.jumpWalks, false);
        struct Label
        {
            const hldata::TouchRect *rect;
            const char *name;
            int touch;
        };
        const Label labels[] = {
            {&at.menu, "MENU", hldata::kTouchMenu},    {&at.use, "USE", hldata::kTouchUse},
            {&at.walk, "WALK", hldata::kTouchWalk},    {&at.reload, "RELOAD", hldata::kTouchReload},
            {&at.jump, "JUMP", hldata::kTouchJump},    {&at.lamp, "BUY", hldata::kTouchLamp},
            {&at.duck, "DUCK", hldata::kTouchDuck},    {&at.pad, nullptr, hldata::kTouchAim},
        };
        for (const Label &l : labels)
            frame(*l.rect, in.touching && out.touchOn == l.touch, l.name);
        static const char *const kSlots[hldata::kTouchSlots] = {"1", "2", "3", "4", "5"};
        for (int n = 0; n < hldata::kTouchSlots; ++n)
            frame(at.slot[n], in.touching && out.touchOn == hldata::kTouchSlot + n, kSlots[n]);
        std::snprintf(line, sizeof line, "%s, %s hand%s", hldata::controlsName(dsSet.scheme),
                      dsSet.leftHanded ? "left" : "right", irrst ? "" : "  (no C-stick)");
        text(8, 60, 0.45f, line);
        text(8, 78, 0.4f, "Select+Left/Right: map   Select+R: layout");
        text(8, 92, 0.4f, "Select+L: hand   Select+Start: quit");

        C3D_FrameEnd(0);
    }

    world.unload();
    C2D_TextBufDelete(textBuf);
    if (irrst)
        irrstExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
