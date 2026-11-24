#include "game/Input.hpp"

#include <nds.h>

#include "Global.hpp"
#include "game/Player.hpp"
#include "Settings.hpp"
#include "audio/Sound.hpp"
#include "HlCsRound.hpp"
#include "net/NetGame.hpp"
#include "render/BottomScreen.hpp"

namespace hlrt
{
    Input input;

    // (HlControls.hpp names the buttons by the bits of the key register.)
    static_assert(hldata::kButtonA == KEY_A && hldata::kButtonB == KEY_B && hldata::kButtonX == KEY_X &&
                  hldata::kButtonY == KEY_Y && hldata::kButtonL == KEY_L && hldata::kButtonR == KEY_R &&
                  hldata::kButtonUp == KEY_UP && hldata::kButtonDown == KEY_DOWN &&
                  hldata::kButtonLeft == KEY_LEFT && hldata::kButtonRight == KEY_RIGHT &&
                  hldata::kButtonSelect == KEY_SELECT && hldata::kButtonStart == KEY_START);

    // The stylus finer than a pixel, by the firmware's own calibration (two
    // points, raw and on the screen, for each axis); and how hard it is
    // pressed, as the touch controller's datasheet has it: the plate's
    // resistance is the raw X times (Z2 / Z1 - 1) -- the firmer, the less.
    // A reading the calibration cannot be trusted for (a DSi's own touch
    // mode reads otherwise), or that has no Z, is left at what it was: a
    // pixel, and no pressure.
    void Input::fine(int rawX, int rawY, int z1, int z2, hldata::ControlsIn &in)
    {
        const PERSONAL_DATA &data = *PersonalData;
        const int spanX = data.calX2 - data.calX1, spanY = data.calY2 - data.calY1;
        const int pxX = data.calX2px - data.calX1px, pxY = data.calY2px - data.calY1px;
        if (spanX != 0 && spanY != 0 && pxX != 0 && pxY != 0)
        {
            const int fx = ((rawX - data.calX1) * pxX * 16) / spanX + (data.calX1px - 1) * 16;
            const int fy = ((rawY - data.calY1) * pxY * 16) / spanY + (data.calY1px - 1) * 16;
            // (Within half a dozen pixels of where the console says it is.)
            if (fx >= 0 && fy >= 0 && fx / 16 - in.touchX < 6 && in.touchX - fx / 16 < 6 &&
                fy / 16 - in.touchY < 6 && in.touchY - fy / 16 < 6)
            {
                in.fineX = fx;
                in.fineY = fy;
            }
        }
        notePressure(z1, z2);
        if (z1 > 0 && z2 > z1 && rawX > 0)
        {
            const int pressure = static_cast<int>((static_cast<std::int64_t>(rawX) * (z2 - z1)) / z1);
            in.pressure = pressure > 0 ? pressure : 1;
        }
    }

    HL_ARM void Input::poll(bool playing)
    {
        scanKeys();
        // What is HELD, and never keysDown():
        // keysDown() only reports what changed since the
        // LAST scan, whoever made it. The edges are worked out from this.
        const std::uint32_t held = keysHeld();
        hldata::ControlsIn in;
        in.held = held;
        in.blank = Global::vblanks();
        // (A touch the menu had, still down: as though it were not.)
        if (touchHeld_ && !(held & KEY_TOUCH))
            touchHeld_ = false;
        if (touchHeld_)
            in.held &= ~static_cast<std::uint32_t>(KEY_TOUCH);
        else if (held & KEY_TOUCH)
        {
            touchPosition touch;
            touchRead(&touch);
            in.touching = true;
            in.touchX = touch.px;
            in.touchY = touch.py;
            fine(touch.rawx, touch.rawy, touch.z1, touch.z2, in);
        }
        // A slot's list of weapons first: a touch that is the list's is not
        // the controls'.
        const bool longJump = (player.arsenal.have & hldata::kArsenalLongJump) != 0;
        // (Counter-Strike's bar is a match's alone too: SPRAY on it.)
        const bool matchBar = netGame.playing() || hldata::activeGame == hldata::kGameCStrike;
        const hldata::TouchLayout at = hldata::touchLayout(settings.leftHanded, matchBar, settings.jumpWalks, longJump);
        const bool landed = in.touching && !touchWas_;
        touchWas_ = in.touching;
        if (in.touching)
            touchY_ = in.touchY;
        // (Counter-Strike's buy menu before it, while it is up: a touch on it
        // is its, and no weapon list comes up under it.)
        const bool cstrike = hldata::activeGame == hldata::kGameCStrike;
        hldata::cs::BuyTouch bought;
        if (cstrike && playing)
            bought = hldata::cs::buyPanelStep(buyPanel_, in.touching, landed, in.touchX, in.touchY);
        const hldata::WeaponListTouch list =
            playing && !buyPanel_.open && !bought.consumed
                ? hldata::weaponListStep(weaponList_, at, in.touching, landed, in.touchX, in.touchY, in.blank)
                : hldata::WeaponListTouch();
        hldata::ControlsIn theirs = in;
        if (list.consumed || bought.consumed)
            theirs.touching = false;
        hldata::ControlSettings controls = settings.controls();
        controls.match = matchBar;
#ifndef HLDS_CHILD
        controls.talk = netGame.talkAllowed();
        controls.openMic = netGame.openMic();
#endif
        controls.longJump = longJump;
        static_cast<hldata::ControlsOut &>(*this) = hldata::controlsStep(controls, theirs, state_);
        weaponChoose = list.chosen;
        // Counter-Strike: the lamp's button (and Select) is BUY -- the menu
        // up in a buy zone, or down again -- and the lamp is never switched.
        csActionHeld = false;
        if (cstrike)
        {
            // The freeze: nobody moves nor fires (the view turns, and the
            // buy menu is open).
            if (hldata::cs::round.phase == hldata::cs::Phase::Freeze)
            {
                moveForward = moveSide = 0;
                jump = attack = second = false;
            }
            // The tactical shield: the second trigger -- or its slot, the
            // primary's -- raises it or lowers it; raised, nothing is fired.
            // (With it, the second trigger is the shield's alone, as the
            // game has it: no silencer, no burst, no stab.)
            {
                static bool secondWas = false;
                const bool secondEdge = second && !secondWas;
                secondWas = second;
                if (playing && hldata::cs::player.shield && !hldata::cs::c4InHand)
                {
                    if (secondEdge || weaponPick == 3)
                        hldata::cs::player.shieldUp = !hldata::cs::player.shieldUp;
                    if (weaponPick == 3)
                        weaponPick = 0;
                    second = false;
                    if (hldata::cs::player.shieldUp)
                        attack = false;
                }
            }
            // The M4A1's and the USP's silencer: the second trigger puts it
            // on or takes it off, as the game's does -- nothing fired till
            // it is done.
            {
                static bool silencerWas = false;
                const bool silencerEdge = second && !silencerWas;
                silencerWas = second;
                bool *silencer = playing && !hldata::cs::c4InHand
                                     ? hldata::cs::silencerOf(player.state.inv.current, hldata::cs::player)
                                     : nullptr;
                if (silencer != nullptr)
                {
                    if (silencerEdge && hldata::cs::silencerMs <= 0)
                    {
                        *silencer = !*silencer;
                        hldata::cs::silencerMs = silencer == &hldata::cs::player.m4Silenced ? 2000 : 3000;
                        hldata::cs::silencerSaid = *silencer ? 1 : 2;
                    }
                    second = false;
                }
                if (hldata::cs::silencerMs > 0)
                    attack = false;
            }
            // The night vision goggles, in slot 5 when it has no bomb (the
            // lower half of it when it has): touched, on or off.
            if (playing && weaponPick == 5 && hldata::cs::player.nvg &&
                (!hldata::cs::round.hasBomb || touchY_ >= at.slot[4].y + at.slot[4].h / 2))
            {
                weaponPick = 0;
                hldata::cs::nvgToggle(hldata::cs::player);
            }
            // The bomb, in slot 5 while it is carried (as the game has it):
            // touched, taken out -- the gun in hand put away -- or put away
            // again. In hand, fire held in a bomb site plants it.
            if (playing && weaponPick == 5 && hldata::cs::round.hasBomb)
            {
                weaponPick = 0;
                if (hldata::cs::c4InHand)
                {
                    hldata::cs::c4InHand = false;
                    weaponChoose = hldata::cs::c4PutAway;
                }
                else
                {
                    hldata::cs::c4InHand = true;
                    hldata::cs::c4PutAway = player.state.inv.current;
                    player.state.inv.current = hldata::kWeaponNone;
                    dropWeaponList();
                }
                sound.ui(hldata::kUiSoundWeapon, 127);
            }
            if (hldata::cs::c4InHand && !hldata::cs::round.hasBomb)
            {
                // (Planted, or the round over: the gun back.)
                hldata::cs::c4InHand = false;
                weaponChoose = hldata::cs::c4PutAway;
            }
            else if (hldata::cs::c4InHand && (weaponPick != 0 || weaponStep != 0 || weaponChoose != 0))
                hldata::cs::c4InHand = false; // (another weapon asked for)
            if (hldata::cs::c4InHand)
            {
                csActionHeld = playing && attack;
                attack = second = false;
            }
            // The button is DEFUSE by a planted bomb: held, not pressed --
            // and then it is not BUY.
            if (hldata::cs::action != hldata::cs::Action::Buy)
            {
                csActionHeld = csActionHeld || (playing && (touchOn == hldata::kTouchLamp || (held & KEY_SELECT) != 0));
                lampPressed = false;
                buyPanel_.open = false;
            }
            const bool buyAsked = lampPressed && playing;
            lampPressed = false;
            if (buyAsked)
            {
                if (buyPanel_.open)
                {
                    buyPanel_.open = false;
                    sound.ui(hldata::kUiSoundHudOff, 127);
                }
                else if (hldata::cs::player.inBuyZone)
                {
                    hldata::cs::openBuy(buyPanel_);
                    dropWeaponList();
                    sound.ui(hldata::kUiSoundHudOn, 127);
                }
                else if (hldata::cs::radioOpen())
                {
                    // (Out of a buy zone the button is RADIO: orders to the
                    // bots on their side, alive or dead -- and the bomb put
                    // down, if they carry it.)
                    hldata::cs::openRadio(buyPanel_);
                    dropWeaponList();
                    sound.ui(hldata::kUiSoundHudOn, 127);
                }
                else
                    sound.ui(hldata::kUiSoundDeny, 127);
            }
            // (Walked out of the zone: the buy menu goes -- not the radio.)
            if (buyPanel_.open && !buyPanel_.direct && !hldata::cs::player.inBuyZone)
                buyPanel_.open = false;
            if (bought.order >= 0)
            {
                // An order given: the bots take it (HlCsBots.hpp's
                // ordersTick), and it is said (the game plays it).
                // (In a match, said to the host too: its bots take it.)
                using hldata::cs::CsSound;
                using hldata::cs::Order;
                static constexpr CsSound kSaid[hldata::cs::kOrderCount] = {
                    CsSound::RadioRoger,    CsSound::RadioGo,       CsSound::RadioGo,       CsSound::RadioStormFront,
                    CsSound::RadioFlank,    CsSound::RadioGetInPos, CsSound::RadioFollowMe, CsSound::RadioHold};
                // (A counter-terrorist's: the enemy spotted -- the bomb going
                // there -- go, get in position, regroup.)
                static constexpr CsSound kCtSaid[hldata::cs::kOrderCount] = {
                    CsSound::RadioRoger,    CsSound::RadioEnemySpot, CsSound::RadioEnemySpot, CsSound::RadioGo,
                    CsSound::RadioGetInPos, CsSound::RadioRegroup,   CsSound::RadioFollowMe,  CsSound::RadioHold};
                const bool t = hldata::cs::player.team == hldata::cs::kTeamT;
                hldata::cs::giveOrder(hldata::cs::player.team, static_cast<Order>(bought.order));
                hldata::cs::radioToSend = bought.order;
                hldata::cs::radioSay = static_cast<int>(
                    bought.order < hldata::cs::kOrderCount ? (t ? kSaid : kCtSaid)[bought.order]
                                                           : (t ? CsSound::RadioGo : CsSound::RadioTakePoint));
            }
            if (bought.dropBomb)
                hldata::cs::bombDropAsked = true;
            if (bought.moved)
                sound.ui(hldata::kUiSoundMove, 127);
            if (bought.paged)
                sound.ui(hldata::kUiSoundPress, 127);
            // PRIMARY AMMO, SECONDARY AMMO: as many boxes as are wanted
            // and paid for, the menu left up.
            if (bought.ammo >= 0)
                sound.ui(hldata::cs::buyAmmo(player.state, hldata::cs::player, bought.ammo) == hldata::cs::Buy::Ok
                             ? hldata::kUiSoundWeapon
                             : hldata::kUiSoundDeny,
                         127);
            if (bought.item != hldata::cs::kNoItem)
            {
                std::uint8_t hand = hldata::kWeaponNone;
                if (hldata::cs::buy(player.state, hldata::cs::player, bought.item, hand) == hldata::cs::Buy::Ok)
                {
                    // (Bought: the menu goes, as the game's does, and a gun
                    // comes to hand.)
                    buyPanel_.open = false;
                    sound.ui(hldata::kUiSoundWeapon, 127);
                    if (hand != hldata::kWeaponNone)
                        weaponChoose = hand;
                }
                else
                    sound.ui(hldata::kUiSoundDeny, 127);
            }
        }
        // (A menu over the game: its touches are the menu's, and no weapon
        // is asked for nor heard.)
        if (!playing)
        {
            weaponPick = weaponStep = 0;
            return;
        }
        // As the game's weapon menu sounds (cl_dll/ammo.cpp): along it, away.
        if (list.moved)
            sound.ui(hldata::kUiSoundMove, 127);
        if (list.dismissed)
            sound.ui(hldata::kUiSoundHudOff, 127);
        // A slot touched: its list up (or, with one weapon in it, that one),
        // and the menu's sound of coming up.
        if (weaponPick != 0)
        {
            weaponChoose = hldata::weaponListOpen(weaponList_, weaponPick, bottomScreen.have(), bottomScreen.empty(), in.blank);
            weaponList_.owned = weaponList_.open();
            if (weaponList_.open() || weaponChoose != 0)
                sound.ui(hldata::kUiSoundHudOn, 127);
            weaponPick = 0;
        }
        // (The one in hand, taken again, is no change -- but Counter-Strike's
        // grenades, taken again, are the next kind of them carried.) A weapon
        // asked for: the game's sound of a choice -- on asking, not when it
        // is drawn.
        if (cstrike && weaponChoose == hldata::kWeaponGrenade && bottomScreen.weapon() == hldata::kWeaponGrenade &&
            hldata::cs::nextGrenade(hldata::cs::player))
            sound.ui(hldata::kUiSoundWeapon, 127);
        if (weaponChoose == bottomScreen.weapon())
            weaponChoose = 0;
        if (weaponChoose != 0 || weaponStep != 0)
            sound.ui(hldata::kUiSoundWeapon, 127);
    }
} // namespace hlrt
