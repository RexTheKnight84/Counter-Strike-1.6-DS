#include "global_header.hpp"

#include "render/BottomScreen.hpp"

#include "HlCs.hpp"
#include "HlCsRound.hpp"
#include "HlPlayerMove.hpp"
#include "HlPreview.hpp"
#include "audio/Sound.hpp"
#include "Settings.hpp"
#include "Global.hpp"
#include "HlTouchLayout.hpp"
#include "HlWeaponList.hpp"
#include "HlUi.hpp"
#include "game/Player.hpp"
#include "net/NetGame.hpp"
#include "ui/TouchCanvas.hpp"

#include <cstring>

namespace hlrt
{
    BottomScreen bottomScreen;

    namespace
    {
        using namespace hldata;
        // showProgress's STOP, and whether it has been touched since.
        constexpr int kStopX = 88, kStopY = 152, kStopW = 80, kStopH = 22;
        volatile bool stopShown = false, stopHit = false;

        // The weapons' pictures (ui/weapons.hic), if the card has them.
        // (Counter-Strike's, ui/csweapons.hic, in its place in its games.)
        constexpr int kIconsMost = kWeaponIcons > cs::kCsIcons ? kWeaponIcons : cs::kCsIcons;
        std::uint32_t iconWords[(sizeof(IconsHeader) + kIconsMost * kIconMaxHeight * iconRowWords(kIconMaxWidth) * 4) / 4];
        IconsView icons;
        bool csIcons = false;
        /// Which of the pictures weapon `weapon` is shown as (-1: none).
        int iconOf(int weapon)
        {
            return csIcons ? cs::iconFor(weapon, cs::player) : weapon - 1;
        }

        /// The play area -- the pad and the column beside it, all the
        /// screen's width, from the bar's foot to the pad's -- copied from
        /// `from` to `to`, both laid out as the screen's: whole tiles, but of
        /// the last row of them only the pixel rows above the slots (which
        /// are drawn on the screen itself, in between).
        void copyPlay(std::uint32_t *to, const std::uint32_t *from)
        {
            const int top = kTouchPadTop / 8, bottom = kTouchPadTop + kTouchPadHeight; // (24: a tile's edge)
            const int whole = bottom / 8;
            std::memcpy(to + top * 32 * 8, from + top * 32 * 8, static_cast<std::size_t>(whole - top) * 32 * 32);
            if (const int rows = bottom - whole * 8)
            {
                for (int tx = 0; tx < 32; ++tx)
                {
                    const int at = (whole * 32 + tx) * 8;
                    std::memcpy(to + at, from + at, static_cast<std::size_t>(rows) * 4);
                }
            }
        }

        // What a control is drawn in: at rest a dim amber line on the
        // screen's own dark, held (or in hand) the full amber on a lit
        // plate.
        void plate(const TouchRect &r, bool lit, bool faint = false)
        {
            touchCanvas.canvas().plate(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4, lit ? kUiPanelDown : kUiBack,
                                       lit ? kUiOrange + 2 : (faint ? kUiPanel : kUiOrange));
        }

        void label(const TouchRect &r, const char *text, bool lit)
        {
            Canvas &canvas = touchCanvas.canvas();
            const FontView &font = touchCanvas.font(kFontSmall);
            plate(r, lit);
            drawTextCentred(canvas, font, r.x + r.w / 2, r.y + (r.h - font.height()) / 2, text,
                            inkFor(font, lit ? kUiWhite : kUiOrange));
        }

        /// A chevron, its point up or down, five lines thick.
        void chevron(int cx, int cy, bool up, int colour)
        {
            Canvas &canvas = touchCanvas.canvas();
            for (int t = 0; t < 5; ++t)
            {
                const int tip = up ? cy - 7 + t : cy + 7 - t, base = up ? cy + 3 + t : cy - 3 - t;
                canvas.line(cx - 11, base, cx, tip, colour);
                canvas.line(cx + 11, base, cx, tip, colour);
            }
        }

        /// A ring, `thick` pixels of it inwards from `radius`.
        void ring(int cx, int cy, int radius, int thick, int colour)
        {
            Canvas &canvas = touchCanvas.canvas();
            const int outer = radius * radius, inner = (radius - thick) * (radius - thick);
            for (int y = -radius; y <= radius; ++y)
            {
                for (int x = -radius; x <= radius; ++x)
                {
                    const int d = x * x + y * y;
                    if (d <= outer && d > inner)
                        canvas.pixel(cx + x, cy + y, colour);
                }
            }
        }
    } // namespace

    void BottomScreen::create()
    {
        touchCanvas.create();
    }

    void BottomScreen::reset()
    {
        touchCanvas.reset();
        shown_ = Shown::Nothing;
    }

    void BottomScreen::release()
    {
        shown_ = Shown::Nothing;
    }

    void BottomScreen::loadIcons()
    {
        const bool cstrike = activeGame == kGameCStrike;
        if (iconsTried_ && iconsFor_ == cstrike)
            return;
        iconsTried_ = true;
        iconsFor_ = cstrike;
        icons = IconsView();
        csIcons = false;
        if (!global.hasData)
            return;
        // (Counter-Strike's own if it has them; else Half-Life's.)
        for (int pass = cstrike ? 0 : 1; pass < 2; ++pass)
        {
            std::FILE *file = std::fopen(global.commonPath(pass == 0 ? cs::kCsIconsFile : kWeaponIconsFile).c_str(), "rb");
            if (file == nullptr)
                continue;
            const std::size_t size = std::fread(iconWords, 1, sizeof(iconWords), file);
            std::fclose(file);
            if (icons.open(reinterpret_cast<const std::uint8_t *>(iconWords), size))
            {
                csIcons = pass == 0;
                return;
            }
        }
    }

    void BottomScreen::drawBar()
    {
        Canvas &canvas = touchCanvas.canvas();
        // (The bar has other buttons in a match: what was there before is
        // cleared, not drawn over.)
        // (Counter-Strike's alone too: SPRAY on it.)
        matchBar_ = netGame.playing() || activeGame == kGameCStrike;
#ifndef HLDS_CHILD
        talkBar_ = netGame.talkAllowed();
#endif
        const TouchLayout at = touchLayout(mirrored_, matchBar_, settings.jumpWalks, longJump_);
        canvas.fill(0, 0, kTouchWidth, kTouchBarHeight, kUiBack);
        label(at.menu, "MENU", held_ == kTouchMenu);
        label(at.use, "USE", held_ == kTouchUse);
        if (at.walk.y == 0)
            label(at.walk, "WALK", walking_ || held_ == kTouchWalk);
        if (at.spray.w > 0)
            label(at.spray, talkBar_ ? "TALK" : "SPRAY", held_ == kTouchSpray);
        // RELOAD is faint while there is nothing to reload: no gun in
        // hand, a full one, or nothing carried for it.
        if (canReload_ || held_ == kTouchReload)
        {
            label(at.reload, "RELOAD", held_ == kTouchReload);
        }
        else
        {
            const FontView &font = touchCanvas.font(kFontSmall);
            plate(at.reload, false, true);
            drawTextCentred(canvas, font, at.reload.x + at.reload.w / 2, at.reload.y + (at.reload.h - font.height()) / 2,
                            "RELOAD", inkFor(font, kUiGrey));
        }
    }

    void BottomScreen::drawPad()
    {
        Canvas &canvas = touchCanvas.canvas();
        const TouchRect r = touchLayout(mirrored_, netGame.playing(), settings.jumpWalks, longJump_).pad;
        canvas.plate(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 6, kUiBack, kUiOrange);
        // The mark of the place in its middle, as faint as a line can be:
        // it is where the stylus rests, not something to read.
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        // (Half-Life's mark is Half-Life's: Counter-Strike's pad has a plain
        // ring, a sight's.)
        if (activeGame == kGameCStrike)
        {
            // The radar: the ring its edge, the player its middle (a mark
            // pointing the way they look: up), each of their side a dot, the
            // bomb a red one.
            ring(cx, cy, 30, 2, kUiPanel);
            canvas.fill(cx - 1, cy - 4, 2, 2, kUiOrange + 2);
            canvas.fill(cx - 2, cy - 2, 4, 2, kUiOrange + 2);
            canvas.fill(cx - 3, cy, 6, 2, kUiOrange + 2);
            for (int n = 0; n < radarCount_; ++n)
            {
                const RadarDot &dot = radar_[n];
                canvas.fill(cx + dot.x - 2, cy + dot.y - 2, 4, 4, kUiShadow);
                canvas.fill(cx + dot.x - 1, cy + dot.y - 1, 3, 3, dot.colour);
            }
            // The round: what is happening and its clock, the score; and a
            // plant or defuse as it goes.
            const FontView &font = touchCanvas.font(kFontSmall);
            drawTextCentred(canvas, font, cx, r.y + 5, roundLine1_, inkFor(font, kUiWhite));
            const FontView &plain = touchCanvas.plain();
            drawTextCentred(canvas, plain, cx, r.y + r.h - plain.height() - 6, roundLine2_, inkPlain(kUiOrange + 2));
            // Who is alive: the counter-terrorists' squares to the left of
            // the middle, the terrorists' to the right (the player's own
            // first on their side, orange), over the score.
            if (teams_[1] + teams_[3] > 0 || teams_[4] != 0)
            {
                const int y = r.y + r.h - plain.height() - 6 - 10;
                const int ctAlive = teams_[0], ctCount = teams_[1], tAlive = teams_[2], tCount = teams_[3];
                const int playerTeam = teams_[4];
                const bool playerAlive = teams_[5] != 0;
                auto square = [&](int x, bool alive, bool mine)
                {
                    canvas.fill(x, y, 7, 7, kUiShadow);
                    canvas.fill(x + 1, y + 1, 5, 5, alive ? (mine ? kUiOrange + 2 : kUiWhite + 2) : kUiPanel);
                };
                // (Each side: the player first, if theirs; then its bots,
                // those up first -- the counts are the bots'.)
                auto side = [&](int team, int botsAlive, int bots, int x0, int step)
                {
                    int k = 0;
                    if (playerTeam == team)
                        square(x0 + step * k++, playerAlive, true);
                    for (int b = 0; b < bots; ++b)
                        square(x0 + step * k++, b < botsAlive, false);
                };
                side(2, ctAlive, ctCount, cx - 8 - 9, -9);
                side(1, tAlive, tCount, cx + 8, 9);
            }
            if (csProgress_ > 0)
            {
                const int wide = r.w - 40, left = r.x + 20, top = cy + 36;
                canvas.fill(left, top, wide, 6, kUiTrack);
                canvas.fill(left, top, wide * (csProgress_ > 256 ? 256 : csProgress_) / 256, 6, kUiOrange + 2);
            }
        }
        else
        {
            ring(cx, cy, 30, 4, kUiPanel);
            for (int t = 0; t < 5; ++t)
            {
                canvas.line(cx - 7 + t, cy - 17, cx + 9 + t, cy + 17, kUiPanel);
                canvas.line(cx - 1 + t, cy - 3, cx - 13 + t, cy + 17, kUiPanel);
            }
            canvas.fill(cx - 12, cy - 17, 10, 3, kUiPanel);
        }
        // ... and a tick at the middle of each side.
        canvas.fill(cx - 8, r.y + 1, 16, 2, kUiOrange);
        canvas.fill(cx - 8, r.y + r.h - 3, 16, 2, kUiOrange);
        canvas.fill(r.x + 1, cy - 8, 2, 16, kUiOrange);
        canvas.fill(r.x + r.w - 3, cy - 8, 2, 16, kUiOrange);
    }

    HL_ARM void BottomScreen::drawColumn()
    {
        Canvas &canvas = touchCanvas.canvas();
        const TouchLayout at = touchLayout(mirrored_, netGame.playing(), settings.jumpWalks, longJump_);
        const FontView &font = touchCanvas.font(kFontSmall);
        // (The jump button asked to walk: WALK is where JUMP was.)
        const bool walkOnTop = at.walk.y != 0;
        const struct
        {
            const TouchRect &r;
            const char *name;
            bool up, lit;
        } buttons[] = {{walkOnTop ? at.walk : at.jump, walkOnTop ? "WALK" : "JUMP", true,
                        walkOnTop ? walking_ || held_ == kTouchWalk : held_ == kTouchJump},
                       {at.duck, "DUCK", false, held_ == kTouchDuck}};
        for (const auto &b : buttons)
        {
            plate(b.r, b.lit);
            const int cx = b.r.x + b.r.w / 2;
            // (Halved for LONG JUMP, it has room for its name and no more.)
            if (b.r.h > 30)
                chevron(cx, b.r.y + (b.up ? 15 : b.r.h - 15), b.up, b.lit ? kUiWhite + 2 : kUiOrange + 1);
            drawTextCentred(canvas, font, cx, b.r.h <= 30 ? b.r.y + (b.r.h - font.height()) / 2 : (b.up ? b.r.y + b.r.h - font.height() - 3 : b.r.y + 3), b.name,
                            inkFor(font, b.lit ? kUiWhite : kUiOrange));
        }
        if (at.longJump.w > 0)
        {
            const bool lit = held_ == kTouchLongJump;
            plate(at.longJump, lit);
            drawTextCentred(canvas, font, at.longJump.x + at.longJump.w / 2,
                            at.longJump.y + (at.longJump.h - font.height()) / 2, "LONG", inkFor(font, lit ? kUiWhite : kUiOrange));
        }
        // Counter-Strike: BUY, and the money, where the lamp would be. Faint
        // out of a buy zone (touched there, it is refused).
        if (activeGame == kGameCStrike)
        {
            const TouchRect &r = at.lamp;
            const bool lit = buy_.open || held_ == kTouchLamp;
            // (Where the bomb can be planted or defused, it is that: held.)
            const bool doing = csAction_ != static_cast<int>(cs::Action::Buy);
            // (Out of a buy zone: RADIO -- their orders, the bomb put down.)
            const bool radio = !doing && !buyZone_ && cs::radioOpen();
            const char *name = csAction_ == static_cast<int>(cs::Action::Plant)    ? "PLANT"
                               : csAction_ == static_cast<int>(cs::Action::Defuse) ? "DEFUSE"
                               : radio                                             ? "RADIO"
                                                                                   : "BUY";
            const bool usable = doing || buyZone_ || radio;
            plate(r, lit, !usable);
            const int ink = !usable ? kUiGrey : (lit ? kUiWhite : kUiOrange);
            drawTextCentred(canvas, font, r.x + r.w / 2, r.y + 3, name, inkFor(font, ink));
            char text[12];
            std::snprintf(text, sizeof(text), "$%d", money_ < 0 ? 0 : money_);
            drawTextCentred(canvas, font, r.x + r.w / 2, r.y + r.h - font.height() - 3, text,
                            inkFor(font, lit ? kUiWhite : kUiOrange));
            return;
        }
        // Between them the lamp: a bulb with its rays, lit while it is
        // alight, and under it what is left in it. Faint with no suit to
        // have one.
        {
            const TouchRect &r = at.lamp;
            const bool lit = lampOn_ || held_ == kTouchLamp;
            plate(r, lit, !lampSuit_);
            const int colour = !lampSuit_ ? kUiGrey : (lit ? kUiWhite + 2 : kUiOrange + 1);
            const int cx = r.x + 13, cy = r.y + 12;
            ring(cx, cy, 5, lampOn_ ? 5 : 2, colour);
            for (int k = 0; k < 3; ++k)
            {
                // (Three rays, out to the side the gauge's numbers are.)
                const int dy = (k - 1) * 5;
                canvas.line(cx + 8, cy + dy, cx + 13, cy + dy + (k - 1) * 2, lampOn_ ? colour : (lampSuit_ ? kUiOrange : kUiGrey));
            }
            if (lampSuit_)
            {
                char text[8];
                std::snprintf(text, sizeof(text), "%d", lampBar_ < 0 ? 0 : lampBar_ * 100 / (r.w - 10));
                drawText(canvas, font, r.x + r.w - 5 - textWidth(font, text), r.y + 12 - font.height() / 2, text,
                         inkFor(font, lit ? kUiWhite : kUiOrange));
                // The gauge.
                const int wide = r.w - 10, left = r.x + 5, top = r.y + r.h - 9;
                canvas.fill(left, top, wide, 4, lit ? kUiBack : kUiTrack);
                canvas.fill(left, top, lampBar_ < 0 ? 0 : lampBar_, 4, lampBar_ * 5 < wide ? kUiRed : kUiOrange + 2);
            }
        }
    }

    void BottomScreen::setLamp(bool suit, bool on, int percent)
    {
        const int bar = suit ? percent * (touchLayout(false).lamp.w - 10) / 100 : -1;
        if (suit == lampSuit_ && on == lampOn_ && bar == lampBar_)
            return;
        lampSuit_ = suit;
        lampOn_ = on;
        lampBar_ = bar;
        if (shown_ == Shown::Pad)
            columnChanged();
    }

    void BottomScreen::setBuy(const cs::BuyPanel &panel, int money, bool inZone)
    {
        const bool panelSame = panel.open == buy_.open && panel.menu == buy_.menu &&
                               (panel.owned ? panel.hover : -1) == (buy_.owned ? buy_.hover : -1);
        const bool buttonSame = money == money_ && inZone == buyZone_ && cs::radioOpen() == radioOpen_;
        if (panelSame && buttonSame)
            return;
        const bool wasOpen = buy_.open;
        buy_ = panel;
        money_ = money;
        buyZone_ = inZone;
        radioOpen_ = cs::radioOpen();
        if (shown_ != Shown::Pad)
            return;
        // (Up, down, another page, a button under the stylus, or money spent
        // with it up: all of it again. Only the button's money: the column.)
        if (!panelSame || panel.open || wasOpen)
            drawPlayArea();
        else
            columnChanged();
    }

    void BottomScreen::setRound(const char *line1, const char *line2, int action, int progress, int bomb)
    {
        // (... and the shield's, carried and raised: bits 4 and 8.)
        const int nvg = (cs::player.nvg ? (cs::player.nvgOn ? 2 : 1) : 0) | (cs::player.shield ? 4 : 0) |
                        (cs::player.shieldUp ? 8 : 0);
        if (bomb != csBomb_ || nvg != csNvg_)
        {
            csBomb_ = bomb;
            csNvg_ = nvg;
            if (shown_ == Shown::Pad)
                drawSlots();
        }
        if (std::strcmp(line1, roundLine1_) == 0 && std::strcmp(line2, roundLine2_) == 0 && action == csAction_ &&
            progress == csProgress_)
            return;
        std::snprintf(roundLine1_, sizeof(roundLine1_), "%s", line1);
        std::snprintf(roundLine2_, sizeof(roundLine2_), "%s", line2);
        csAction_ = action;
        csProgress_ = progress;
        if (shown_ == Shown::Pad)
            drawPlayArea();
    }

    void BottomScreen::setTeams(int ctAlive, int ctCount, int tAlive, int tCount, int playerTeam, bool playerAlive)
    {
        const int now[6] = {ctAlive, ctCount, tAlive, tCount, playerTeam, playerAlive ? 1 : 0};
        bool same = true;
        for (int k = 0; k < 6; ++k)
            same = same && now[k] == teams_[k];
        if (same)
            return;
        for (int k = 0; k < 6; ++k)
            teams_[k] = now[k];
        if (shown_ == Shown::Pad && !buy_.open && !list_.open())
            drawPlayArea();
    }

    void BottomScreen::setRadar(const RadarDot *dots, int count)
    {
        count = count < 0 ? 0 : (count > kRadarDots ? kRadarDots : count);
        if (radarWait_ > 0)
            --radarWait_;
        bool same = count == radarCount_;
        for (int n = 0; same && n < count; ++n)
            same = dots[n].x == radar_[n].x && dots[n].y == radar_[n].y && dots[n].colour == radar_[n].colour;
        if (same || radarWait_ > 0)
            return;
        for (int n = 0; n < count; ++n)
            radar_[n] = dots[n];
        radarCount_ = count;
        // (The play area is drawn whole: a few times a second at most.)
        radarWait_ = 8;
        if (shown_ == Shown::Pad && !buy_.open && !list_.open())
            drawPlayArea();
    }

    /// Counter-Strike's buy menu: its head (BACK on a kind's page, which
    /// page, the money), then two columns of buttons -- the kinds and CLOSE,
    /// or a kind's items, each its name and its price, grey if it cannot be
    /// bought.
    HL_ARM void BottomScreen::drawBuyPanel()
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &font = touchCanvas.font(kFontSmall);
        const TouchRect area = cs::buyPanelRect();
        canvas.fill(area.x, area.y, area.w, area.h, kUiBack);
        canvas.frame(area.x, area.y, area.w, area.h, kUiOrange + 1);
        const int hover = buy_.owned ? buy_.hover : -1;
        const int headY = cs::kBuyTop + (cs::kBuyHeadHeight - font.height()) / 2;
        const bool radio = buy_.menu == cs::kMenuRadio;
        if (buy_.menu >= 0)
        {
            const TouchRect back = cs::buyBackRect();
            canvas.button(back.x, back.y, back.w, back.h, hover == cs::kBuyHoverBack);
            drawTextCentred(canvas, font, back.x + back.w / 2, back.y + (back.h - font.height()) / 2, "BACK",
                            inkFor(font, hover == cs::kBuyHoverBack ? kUiWhite : kUiOrange));
        }
        drawTextCentred(canvas, font, kTouchWidth / 2, headY,
                        radio ? "RADIO" : (buy_.menu >= 0 ? cs::kMenuNames[buy_.menu] : "BUY"), inkFor(font, kUiWhite));
        char text[16];
        std::snprintf(text, sizeof(text), "$%d", money_ < 0 ? 0 : money_);
        drawText(canvas, font, kTouchWidth - 6 - textWidth(font, text), headY, text, inkFor(font, kUiOrange));

        if (buy_.menu < 0)
        {
            const int close = cs::buyCloseCell();
            for (int c = 0; c <= close; ++c)
            {
                const TouchRect r = cs::buyCellRect(c);
                const bool under = hover == c;
                canvas.button(r.x, r.y, r.w, r.h, under);
                drawTextCentred(canvas, font, r.x + r.w / 2, r.y + (r.h - font.height()) / 2,
                                c == close                         ? "CLOSE"
                                : c == cs::kBuyRadioCell           ? "RADIO"
                                : c == cs::kBuyPrimaryAmmoCell     ? "PRIMARY AMMO"
                                : c == cs::kBuySecondaryAmmoCell   ? "SECONDARY AMMO"
                                                                   : cs::kMenuNames[c],
                                inkFor(font, under ? kUiWhite : kUiOrange));
            }
            return;
        }
        if (radio)
        {
            // The orders, then the map's ways; the one they are under lit.
            const int cells = cs::radioCells();
            for (int c = 0; c < cells; ++c)
            {
                const TouchRect r = cs::buyCellRect(c, cells);
                const bool under = hover == c;
                const bool now = c < cs::radioOrderCells() &&
                                 static_cast<int>(cs::teamOrder[cs::teamSlot(cs::player.team)]) == c;
                canvas.button(r.x, r.y, r.w, r.h, under || now);
                drawTextCentred(canvas, font, r.x + r.w / 2, r.y + (r.h - font.height()) / 2, cs::orderName(c),
                                inkFor(font, under || now ? kUiWhite : kUiOrange));
            }
            return;
        }
        // (The ROM's own 8x8 letters: 1.6's name for each, as its buy menu
        // has it for the team -- the guns' one to a row, the price beside
        // it; the equipment's two to a row, the price under the name.)
        const FontView &plain = touchCanvas.plain();
        int items[cs::kBuyMost];
        const int n = cs::itemsOfMenu(buy_.menu, items, cs::kBuyMost, cs::player.team);
        const int columns = cs::buyColumns(buy_.menu);
        for (int c = 0; c < n; ++c)
        {
            const cs::Item &item = cs::kItems[items[c]];
            const char *name = item.label != nullptr ? item.label : item.name;
            const TouchRect r = cs::buyCellRect(c, n, columns);
            const bool under = hover == c;
            const cs::Buy why = cs::canBuy(player.state, cs::player, items[c]);
            const bool grey = why == cs::Buy::OtherTeam || why == cs::Buy::NoMoney || why == cs::Buy::Have;
            canvas.button(r.x, r.y, r.w, r.h, under);
            const int ink = grey ? kUiGrey + 2 : (under ? kUiWhite + 2 : kUiOrange + 2);
            std::snprintf(text, sizeof(text), "$%d", static_cast<int>(item.price));
            const int margin = textWidth(plain, name) + 8 > r.w ? 2 : 4;
            if (columns == 1)
            {
                const int y = r.y + (r.h - plain.height()) / 2;
                drawText(canvas, plain, r.x + margin, y, name, inkPlain(ink));
                drawText(canvas, plain, r.x + r.w - margin - textWidth(plain, text), y, text,
                         inkPlain(grey ? kUiGrey + 1 : kUiWhite + 1));
            }
            else
            {
                drawText(canvas, plain, r.x + margin, r.y + 2, name, inkPlain(ink));
                drawText(canvas, plain, r.x + r.w - 4 - textWidth(plain, text), r.y + r.h - plain.height() - 2, text,
                         inkPlain(grey ? kUiGrey + 1 : kUiWhite + 1));
            }
        }
    }

    HL_ARM void BottomScreen::drawSlots()
    {
        // (What a slot says with no picture to show: by weapon.)
        static const char *const kNames[kWeaponCount] = {"",     "BAR",  "9MM",   "SHOT", "GREN", "MP5",
                                                         ".357", "XBOW", "SATCH", "MINE", "SNARK",
                                                         "RPG",  "TAU",  "EGON",  "HIVE"};
        Canvas &canvas = touchCanvas.canvas();
        const TouchLayout at = touchLayout(mirrored_, netGame.playing(), settings.jumpWalks, longJump_);
        const FontView &font = touchCanvas.font(kFontSmall);
        loadIcons();
        // (Lit, besides the one in hand: the slot touched, and the one whose
        // list is up.)
        const int touched = held_ - kTouchSlot, listed = list_.open() && !list_.closing ? list_.category - 1 : -1;
        for (int n = 0; n < kTouchSlots; ++n)
        {
            // The game's slot n + 1, a kind of weapon: shown as the one of
            // them in hand, or else the first of them carried.
            const TouchRect &r = at.slot[n];
            // (Counter-Strike: slot 5 is the bomb while it is carried, lit
            // in hand.)
            // ... and the night vision goggles, if they are carried: lit
            // while on -- the slot's lower half, with the bomb in it too.
            // (Counter-Strike: the tactical shield, in the primary's slot --
            // lit while raised.)
            if (n == 2 && activeGame == kGameCStrike && (csNvg_ & 4))
            {
                const bool lit = (csNvg_ & 8) != 0 || n == touched;
                plate(r, lit);
                drawTextCentred(canvas, font, r.x + r.w / 2, r.y + (r.h - font.height()) / 2, "SHIELD",
                                inkFor(font, lit ? kUiWhite : kUiOrange));
                continue;
            }
            const int nvgShown = csNvg_ & 3;
            if (n == 4 && activeGame == kGameCStrike && (csBomb_ != 0 || nvgShown != 0))
            {
                const bool lit = csBomb_ == 2 || (csBomb_ == 0 && nvgShown == 2) || n == touched;
                plate(r, lit);
                const int cx = r.x + r.w / 2;
                if (csBomb_ != 0 && nvgShown != 0)
                {
                    const int mid = r.y + r.h / 2;
                    canvas.line(r.x + 4, mid, r.x + r.w - 5, mid, kUiOrange);
                    drawTextCentred(canvas, font, cx, r.y + (r.h / 2 - font.height()) / 2 + 1, "C4",
                                    inkFor(font, csBomb_ == 2 ? kUiWhite : kUiRed));
                    drawTextCentred(canvas, font, cx, mid + (r.h / 2 - font.height()) / 2, "NVG",
                                    inkFor(font, nvgShown == 2 ? kUiWhite : kUiOrange));
                }
                else if (csBomb_ != 0)
                    drawTextCentred(canvas, font, cx, r.y + (r.h - font.height()) / 2, "C4",
                                    inkFor(font, lit ? kUiWhite : kUiRed));
                else
                    drawTextCentred(canvas, font, cx, r.y + (r.h - font.height()) / 2, "NVG",
                                    inkFor(font, lit ? kUiWhite : kUiOrange));
                continue;
            }
            int count = 0;
            const std::uint8_t *list = weaponsOfCategory(n + 1, count);
            int weapon = kWeaponNone;
            for (int k = 0; k < count && weapon == kWeaponNone; ++k)
            {
                if (list[k] == weapon_ && (have_ & (1u << list[k])))
                    weapon = list[k];
            }
            for (int k = 0; k < count && weapon == kWeaponNone; ++k)
            {
                if (have_ & (1u << list[k]))
                    weapon = list[k];
            }
            const bool carried = weapon != kWeaponNone, inHand = carried && weapon == weapon_;
            // Lit in hand; and while it is touched, or its list is up.
            const bool lit = inHand || (carried && (n == touched || n == listed));
            plate(r, lit, !carried);
            if (!carried)
                continue;
            const int cx = r.x + r.w / 2;
            const int top = r.y + (inHand && weapon != kWeaponCrowbar ? 3 : (r.h - icons.height()) / 2);
            const int icon = iconOf(weapon);
            if (icon >= 0 && icon < icons.count())
            {
                // The game's picture: dim while it waits, bright in hand.
                const int inks[4] = {0, lit ? kUiOrange + 1 : kUiOrange, lit ? kUiOrange + 2 : kUiOrange + 1,
                                     lit ? kUiWhite + 2 : kUiOrange + 1};
                const int left = cx - icons.width() / 2;
                for (int y = 0; y < icons.height(); ++y)
                {
                    for (int x = 0; x < icons.width(); ++x)
                    {
                        if (const int level = icons.level(icon, x, y))
                            canvas.pixel(left + x, top + y, inks[level]);
                    }
                }
            }
            else
            {
                drawTextCentred(canvas, font, cx, r.y + (inHand && weapon != kWeaponCrowbar ? 3 : (r.h - font.height()) / 2),
                                kNames[weapon], inkFor(font, lit ? kUiWhite : kUiOrange));
            }
            if (!inHand || weapon == kWeaponCrowbar)
                continue;
            // Under it, what is in it and what is carried for it -- or as
            // much of that as the slot is wide. (What is thrown or put down
            // is a count of itself.)
            char text[16];
            std::snprintf(text, sizeof(text), "%d/%d", clip_, spare_);
            const bool counted = weapon == kWeaponGrenade || weapon == kWeaponSatchel || weapon == kWeaponTripmine ||
                                 weapon == kWeaponSnark || weapon == kWeaponGauss || weapon == kWeaponEgon ||
                                 weapon == kWeaponHornetgun;
            if (counted || textWidth(font, text) > r.w - 6)
                std::snprintf(text, sizeof(text), "%d", clip_);
            drawTextCentred(canvas, font, cx, r.y + r.h - font.height() - 1, text, inkFor(font, kUiWhite));
        }
    }

    void BottomScreen::setWeaponList(const WeaponList &list)
    {
        const int hover = list.owned ? list.hover : -1;
        if (list.category == list_.category && list.slide == list_.slide && hover == listHover_ &&
            list.count == list_.count)
            return;
        const bool slot = list.category != list_.category || list.closing != list_.closing;
        list_ = list;
        listHover_ = hover;
        if (shown_ != Shown::Pad)
            return;
        drawPlayArea();
        // (Its slot is lit while it is up.)
        if (slot)
            drawSlots();
    }

    void BottomScreen::drawPlayArea()
    {
        // What was under it, then it where it is now -- off the screen, from
        // the screen as it is: put on it whole, in the next vertical blank
        // (present). (It covers the column too, for a slot at the side.)
        copyPlay(touchCanvas.copyTiles(), touchCanvas.screenTiles());
        touchCanvas.offScreen(touchCanvas.copyTiles());
        touchCanvas.canvas().fill(0, kTouchPadTop, kTouchWidth, kTouchPadHeight, kUiBack);
        drawPad();
        drawColumn();
        if (buy_.open)
            drawBuyPanel();
        else if (list_.open())
            drawWeaponList();
        touchCanvas.onScreen();
        padPending_ = true;
    }

    [[gnu::noinline]] void BottomScreen::columnChanged()
    {
        if (list_.open() || buy_.open || padPending_)
            drawPlayArea();
        else
            drawColumn();
    }

    void BottomScreen::present()
    {
        if (!padPending_)
            return;
        padPending_ = false;
        if (shown_ == Shown::Pad)
            copyPlay(touchCanvas.screenTiles(), touchCanvas.copyTiles());
    }

    /// The list of a slot's weapons: each its picture and its name, the one
    /// in hand lit, the one under the stylus pressed, one with nothing to
    /// fire grey; risen out of the pad's bottom edge as far as it has slid.
    HL_ARM void BottomScreen::drawWeaponList()
    {
        static const char *const kFull[kWeaponCount] = {"",         "CROWBAR",  "9MM PISTOL", "SHOTGUN",  "GRENADES",
                                                        "MP5",      ".357",     "CROSSBOW",   "SATCHELS", "TRIPMINES",
                                                        "SNARKS",   "RPG",      "TAU CANNON", "GLUON GUN", "HIVEHAND"};
        const TouchLayout at = touchLayout(mirrored_, netGame.playing(), settings.jumpWalks, longJump_);
        const TouchRect r = weaponListRect(list_, at);
        const int drop = (256 - list_.slide) * (r.h + 4) / 256;
        // (Drawn into the screen's rows down to the pad's bottom edge, and no
        // further -- what is still below it is not seen, and the slots are
        // never touched: the play area drawn again puts all of it right.)
        Canvas band = touchCanvas.canvas();
        band.height = weaponListFloor(at) / 8 * 8;
        const FontView &font = touchCanvas.font(kFontSmall);
        loadIcons();
        const int top = r.y + drop;
        band.plate(r.x, top, r.w, r.h, 4, kUiBack, kUiOrange + 2);
        for (int n = 0; n < list_.count; ++n)
        {
            const int weapon = list_.weapons[n];
            const int y = top + 2 + n * kWeaponListRow;
            const bool inHand = weapon == weapon_, under = n == listHover_;
            const bool dry = (list_.empty & (1u << weapon)) != 0;
            band.button(r.x + 3, y + 1, r.w - 6, kWeaponListRow - 2, under);
            if (inHand)
                band.frame(r.x + 3, y + 1, r.w - 6, kWeaponListRow - 2, kUiOrange + 2);
            const int icon = iconOf(weapon);
            if (icon >= 0 && icon < icons.count())
            {
                const int base = dry ? kUiGrey : kUiOrange;
                const int inks[4] = {0, base, base + 1, dry ? kUiGrey + 2 : kUiWhite + 2};
                const int left = r.x + 6, iconTop = y + (kWeaponListRow - icons.height()) / 2;
                for (int py = 0; py < icons.height(); ++py)
                {
                    for (int px = 0; px < icons.width(); ++px)
                    {
                        if (const int level = icons.level(icon, px, py))
                            band.pixel(left + px, iconTop + py, inks[level]);
                    }
                }
            }
            drawText(band, font, r.x + 50, y + (kWeaponListRow - font.height()) / 2, kFull[weapon],
                     inkFor(font, dry ? kUiGrey : (inHand || under ? kUiWhite : kUiOrange)));
        }
    }

    void BottomScreen::showPad()
    {
        stopShown = false;
        create();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll(); // (a menu's colours of its own go)
        shown_ = Shown::Pad;
        padPending_ = false; // (drawn on the screen now, all of it)
        mirrored_ = settings.leftHanded;
        drawBar();
        drawPad();
        drawColumn();
        drawSlots();
        if (buy_.open)
            drawBuyPanel();
        else if (list_.open())
            drawWeaponList();
    }

    void BottomScreen::setStatus(int weapon, int clip, int spare, unsigned have, unsigned empty, bool canReload)
    {
        const bool matchBar = netGame.playing() || activeGame == kGameCStrike;
#ifndef HLDS_CHILD
        const bool bar = canReload != canReload_ || matchBar != matchBar_ || netGame.talkAllowed() != talkBar_;
#else
        const bool bar = canReload != canReload_ || matchBar != matchBar_;
#endif
        // (Counter-Strike: what was bought for each slot -- its picture.)
        const int bought = cs::player.primary * 1000 + cs::player.pistol * 8 + cs::grenadeInHand(cs::player);
        const bool slots = weapon != weapon_ || clip != clip_ || spare != spare_ || have != have_ || bought != bought_;
        bought_ = bought;
        empty_ = empty;
        canReload_ = canReload;
        weapon_ = weapon;
        clip_ = clip;
        spare_ = spare;
        have_ = have;
        if (shown_ != Shown::Pad)
            return;
        if (bar)
            drawBar();
        if (slots)
            drawSlots();
    }

    void BottomScreen::setWalking(bool walking)
    {
        if (walking == walking_)
            return;
        walking_ = walking;
        if (shown_ != Shown::Pad)
            return;
        if (touchLayout(mirrored_, netGame.playing(), settings.jumpWalks, longJump_).walk.y == 0)
            drawBar();
        else
            columnChanged();
    }

    void BottomScreen::setLongJump(bool has)
    {
        if (has == longJump_)
            return;
        longJump_ = has;
        if (shown_ != Shown::Pad)
            return;
        // (The top of the column is cut differently: what was there before is
        // cleared, not drawn over.)
        const TouchRect lamp = touchLayout(mirrored_, netGame.playing(), settings.jumpWalks, longJump_).lamp;
        touchCanvas.canvas().fill(lamp.x, kTouchBarHeight, lamp.w, lamp.y - kTouchBarHeight, kUiBack);
        columnChanged();
    }

    void BottomScreen::setPressed(int held)
    {
        if (held == held_)
            return;
        const int was = held_;
        held_ = held;
        if (shown_ != Shown::Pad)
            return;
        auto onBar = [](int on)
        { return on == kTouchMenu || on == kTouchUse || on == kTouchReload || on == kTouchSpray || on == kTouchWalk; };
        if (onBar(was) || onBar(held))
            drawBar();
        auto onColumn = [](int on) { return on == kTouchJump || on == kTouchDuck || on == kTouchLamp || on == kTouchWalk || on == kTouchLongJump; };
        if (onColumn(was) || onColumn(held))
            columnChanged();
        // (A slot is lit while the stylus is on it, as well as in hand.)
        auto onSlot = [](int on) { return on >= kTouchSlot && on < kTouchSlot + kTouchSlots; };
        if (onSlot(was) || onSlot(held))
            drawSlots();
    }

    void BottomScreen::showMessage(const char *line1, const char *line2)
    {
        stopShown = false;
        create();
        touchCanvas.pageOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle), &font = touchCanvas.font(kFontMedium);
        drawTextCentred(canvas, title, 128, 84, line1, inkFor(title, kUiOrange));
        // (As much of the second line as the screen is wide.)
        char cut[64];
        std::snprintf(cut, sizeof(cut), "%.*s", textFit(font, line2, 248), line2);
        drawTextCentred(canvas, font, 128, 112, cut, inkFor(font, kUiWhite));
        touchCanvas.pageOn();
    }

    void BottomScreen::showCsScores(const char *title)
    {
        stopShown = false;
        create();
        touchCanvas.pageOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &big = touchCanvas.font(kFontTitle), &font = touchCanvas.font(kFontSmall);
        int y = 4;
        drawTextCentred(canvas, big, 128, y, title, inkFor(big, kUiOrange + 2));
        y += big.height() + 6;
        const cs::Round &r = cs::round;
        const std::uint8_t mine = cs::setup.team;
        // Each team a column -- the player's own on the left: its name and
        // rounds, then its people, the most kills first, with their kills
        // and deaths.
        const int row = font.height() + 1;
        for (int t = 0; t < 2; ++t)
        {
            const int x = 2 + t * 128, w = 124;
            const std::uint8_t team = t == 0 ? mine : (mine == cs::kTeamT ? cs::kTeamCT : cs::kTeamT);
            const int side = team == mine ? 1 : 0; // (the roster's)
            int ty = y;
            char head[24];
            std::snprintf(head, sizeof(head), "%s %d", team == cs::kTeamT ? "T" : "CT",
                          team == cs::kTeamT ? r.scoreT : r.scoreCT);
            canvas.fill(x, ty, w, row + 1, kUiOrange);
            drawText(canvas, font, x + 4, ty + 1, head, inkFor(font, kUiWhite + 2));
            drawText(canvas, font, x + 86, ty + 1, "K", inkFor(font, kUiWhite + 2));
            drawText(canvas, font, x + 108, ty + 1, "D", inkFor(font, kUiWhite + 2));
            ty += row + 3;
            struct Line
            {
                const char *name;
                int kills, deaths;
                bool you;
            } lines[1 + cs::kBotSlotsPerSide];
            int count = 0;
            if (side == 1)
                lines[count++] = {"YOU", cs::scores.player.kills, cs::scores.player.deaths, true};
            for (int n = 0; n < cs::roster.count[side] && count < 1 + cs::kBotSlotsPerSide; ++n)
                lines[count++] = {cs::botName(team, n), cs::scores.bot[side][n].kills, cs::scores.bot[side][n].deaths,
                                  false};
            for (int i = 1; i < count; ++i)
            {
                const Line one = lines[i];
                int at = i;
                for (; at > 0 && (lines[at - 1].kills < one.kills ||
                                  (lines[at - 1].kills == one.kills && lines[at - 1].deaths > one.deaths));
                     --at)
                    lines[at] = lines[at - 1];
                lines[at] = one;
            }
            for (int i = 0; i < count; ++i)
            {
                const int ink = lines[i].you ? kUiOrange + 2 : kUiWhite + 1;
                char name[16], k[8], d[8];
                std::snprintf(name, sizeof(name), "%.*s", textFit(font, lines[i].name, 78), lines[i].name);
                std::snprintf(k, sizeof(k), "%d", lines[i].kills);
                std::snprintf(d, sizeof(d), "%d", lines[i].deaths);
                drawText(canvas, font, x + 4, ty, name, inkFor(font, ink));
                drawText(canvas, font, x + 86, ty, k, inkFor(font, ink));
                drawText(canvas, font, x + 108, ty, d, inkFor(font, ink));
                ty += row;
            }
        }
        touchCanvas.pageOn();
    }

    void BottomScreen::drawScores(int top)
    {
        Canvas &canvas = touchCanvas.canvas();
        const NetMatch &match = netGame.match();
        // Who is here, the most frags first (then the fewest deaths).
        int order[kNetPlayers], count = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            if (!netHere(match, p))
                continue;
            int at = count++;
            for (; at > 0; --at)
            {
                const int q = order[at - 1];
                if (match.frags[q] > match.frags[p] ||
                    (match.frags[q] == match.frags[p] && match.deaths[q] <= match.deaths[p]))
                    break;
                order[at] = q;
            }
            order[at] = p;
        }
        // Whole rows of cells (each player's colours have cells of their
        // own): two to a player while they fit, one, in the plain letters,
        // when they do not.
        const int firstCell = (top + 7) / 8 + 1;
        const bool roomy = firstCell + count * 2 <= 24;
        const int rowCells = roomy ? 2 : 1;
        const FontView &font = roomy ? touchCanvas.font(kFontSmall) : touchCanvas.plain();
        canvas.fill(0, top, 256, 192 - top, kUiBack);
        const int headY = firstCell * 8 - font.height() - 1;
        constexpr int kNameX = 28, kFragsX = 196, kDeathsX = 236;
        char text[24];
        if (match.rules.teams())
        {
            std::snprintf(text, sizeof(text), "TEAMS  %d : %d", match.teamFrags[1], match.teamFrags[2]);
            drawText(canvas, font, kNameX, headY, text, inkFor(font, kUiOrange));
        }
        else
            drawText(canvas, font, kNameX, headY, "PLAYER", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, kFragsX, headY, "FRAGS", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, kDeathsX, headY, "DIED", inkFor(font, kUiGrey));
        for (int row = 0; row < count; ++row)
        {
            const int p = order[row], cell = firstCell + row * rowCells, y = cell * 8;
            const int textY = y + (rowCells * 8 - font.height()) / 2;
            if (p == match.me)
                canvas.fill(24, y, 232, rowCells * 8, kUiPanel);
            // The player's own colours (TouchCanvas::tint: its 1 and 2).
            canvas.fill(8, y, 8, 8, 1);
            canvas.fill(16, y, 8, 8, 2);
            const NetProfile &profile = match.profiles[p];
            const std::uint16_t three[3] = {hueColour(profile.known ? profile.top : 30),
                                            hueColour(profile.known ? profile.bottom : 6), uiColour(kUiEdge)};
            touchCanvas.tint(1, cell, 2, 1, three, 2 + row);
            const int ink = p == match.me ? kUiWhite : kUiOrange;
            std::snprintf(text, sizeof(text), "%.*s", textFit(font, match.names[p][0] != 0 ? match.names[p] : "...", 140),
                          match.names[p][0] != 0 ? match.names[p] : "...");
            drawText(canvas, font, kNameX + 2, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", match.frags[p]);
            drawTextCentred(canvas, font, kFragsX, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", match.deaths[p]);
            drawTextCentred(canvas, font, kDeathsX, textY, text, inkFor(font, ink));
        }
    }

    void BottomScreen::drawFinal(int top)
    {
        Canvas &canvas = touchCanvas.canvas();
        const NetMatch &match = netGame.match();
        const NetFinal &final = match.final;
        const bool totals = match.rules.totals;
        const FontView &small = touchCanvas.font(kFontSmall);
        // Whole rows of cells, as drawScores'; the last row is the facts'.
        const int firstCell = (top + 7) / 8 + 1;
        const bool roomy = firstCell + final.count * 2 <= 21;
        const int rowCells = roomy ? 2 : 1;
        const FontView &font = roomy ? small : touchCanvas.plain();
        const int headY = firstCell * 8 - font.height() - 1;
        const int nameX = 28, fragsX = totals ? 166 : 196, deathsX = totals ? 202 : 236, allX = 238;
        char text[48];
        if (match.rules.teams())
        {
            std::snprintf(text, sizeof(text), "TEAMS  %d : %d", final.teamFrags[1], final.teamFrags[2]);
            drawText(canvas, font, nameX, headY, text, inkFor(font, kUiOrange));
        }
        else
            drawText(canvas, font, nameX, headY, "PLAYER", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, fragsX, headY, "FRAGS", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, deathsX, headY, "DIED", inkFor(font, kUiGrey));
        if (totals)
            drawTextCentred(canvas, font, allX, headY, "ALL", inkFor(font, kUiGrey));
        const NetFinal::Row *first = final.count > 0 ? &final.rows[final.order[0]] : nullptr;
        for (int row = 0; row < final.count; ++row)
        {
            const int p = final.order[row], cell = firstCell + row * rowCells, y = cell * 8;
            const NetFinal::Row &line = final.rows[p];
            const int textY = y + (rowCells * 8 - font.height()) / 2;
            // The winner, or the winning team, stands out; who has gone is grey.
            const bool won = match.rules.teams() ? final.winnerTeam != 0 && line.team == final.winnerTeam
                                                 : line.frags == first->frags && line.deaths == first->deaths;
            if (won && !line.gone)
                canvas.fill(24, y, 232, rowCells * 8, kUiPanel);
            canvas.fill(8, y, 8, 8, 1);
            canvas.fill(16, y, 8, 8, 2);
            const std::uint16_t three[3] = {line.gone ? uiColour(kUiGrey) : hueColour(line.top),
                                            line.gone ? uiColour(kUiGrey) : hueColour(line.bottom), uiColour(kUiEdge)};
            touchCanvas.tint(1, cell, 2, 1, three, 2 + row);
            const int ink = line.gone ? kUiGrey : (won || p == match.me ? kUiWhite : kUiOrange);
            const char *name = line.name[0] != 0 ? line.name : "...";
            std::snprintf(text, sizeof(text), "%.*s", textFit(font, name, fragsX - nameX - 18), name);
            drawText(canvas, font, nameX + 2, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", line.frags);
            drawTextCentred(canvas, font, fragsX, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", line.deaths);
            drawTextCentred(canvas, font, deathsX, textY, text, inkFor(font, ink));
            if (totals)
            {
                std::snprintf(text, sizeof(text), "%d", match.sessionFrags[p]);
                drawTextCentred(canvas, font, allX, textY, text, inkFor(font, ink));
            }
        }
        // What comes next, and what was remarkable about it, along the bottom.
        if (match.nextName[0] != 0)
        {
            std::snprintf(text, sizeof(text), "NEXT: %s", match.nextName);
            drawTextCentred(canvas, small, 128, 192 - 2 * small.height() - 4, text, inkFor(small, kUiOrange));
        }
        char facts[64];
        netFinalFacts(final, facts, sizeof(facts));
        if (facts[0] != 0)
        {
            std::snprintf(text, sizeof(text), "%.*s", textFit(small, facts, 248), facts);
            drawTextCentred(canvas, small, 128, 192 - small.height() - 2, text, inkFor(small, kUiGrey));
        }
    }

    namespace
    {
        // The vote's big buttons, three to the screen: its picture on whole
        // cells, its name and its votes beside it.
        constexpr int kVoteTop = 3, kVoteCellsDown = 7, kVoteX = 3, kVoteW = 240;
        int voteRowY(int row) { return (kVoteTop + row * kVoteCellsDown) * 8; }
        constexpr int kLobbyX = 204, kLobbyY = 1, kLobbyW = 50, kLobbyH = 15;

        void drawLobbyButton(Canvas &canvas)
        {
#ifndef HLDS_CHILD
            if (!netGame.isHost())
                return;
            const FontView &small = touchCanvas.font(kFontSmall);
            canvas.button(kLobbyX, kLobbyY, kLobbyW, kLobbyH, false);
            drawTextCentred(canvas, small, kLobbyX + kLobbyW / 2, kLobbyY + (kLobbyH - small.height()) / 2, "LOBBY",
                            inkFor(small, kUiOrange));
#else
            (void)canvas;
#endif
        }
    } // namespace

    bool BottomScreen::lobbyButtonAt(int x, int y) const
    {
        return x >= kLobbyX && x < kLobbyX + kLobbyW && y >= kLobbyY && y < kLobbyY + kLobbyH;
    }

    int BottomScreen::voteRowMap(int row) const
    {
        if (row < 0 || row >= kNetVoteRows)
            return -1;
        switch (votePage_)
        {
        case VotePage::Confirm: return row == 1 ? voteAsked_ : -1;
        case VotePage::Result: return row == 0 ? netGame.match().vote.decided : -1;
        default: return voteSlotMap_[row];
        }
    }

    int BottomScreen::voteTop() const { return netVoteFirstShown(voteScroll_); }

    void BottomScreen::voteRowAt(int map, int y)
    {
        Canvas &canvas = touchCanvas.canvas();
        const NetMatch &match = netGame.match();
        const NetVote &vote = match.vote;
        const FontView &medium = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall);
        // (Not over its picture, which stays as it is: only what is beside it.)
        canvas.fill(kVoteX + 72, y - 3, kVoteW - 72, 54, kUiBack);
        if (map < 0)
            return;
        const bool mine = vote.choice[match.me] == map;
        const bool won = vote.decided == map;
        const int edge = mine || won ? kUiOrange + 2 : kUiEdge;
        canvas.fill(kVoteX, y - 3, kVoteW, 1, edge);
        canvas.fill(kVoteX, y + 50, kVoteW, 1, edge);
        canvas.fill(kVoteX, y - 3, 1, 54, edge);
        canvas.fill(kVoteX + kVoteW - 1, y - 3, 1, 54, edge);
        drawText(canvas, medium, kVoteX + 72, y + 8, vote.names[map], inkFor(medium, mine || won ? kUiWhite : kUiOrange));
        char text[24];
        std::snprintf(text, sizeof(text), "%d vote%s%s", vote.tally[map], vote.tally[map] == 1 ? "" : "s", mine ? " - YOURS" : "");
        drawText(canvas, small, kVoteX + 72, y + 28, text, inkFor(small, mine ? kUiOrange : kUiGrey));
    }

    void BottomScreen::voteRow(int row)
    {
        voteRowAt(voteRowMap(row), voteRowY(row));
    }

    /// A button's place on the list's layer: four slots, the layer's height.
    namespace
    {
        int voteSlotY(int slot) { return slot * kNetVoteRowPixels + 8; }
    }

    void BottomScreen::voteSlots()
    {
        // What is in sight, each in its slot: drawn as it comes in, where it
        // cannot be seen, and nothing else touched.
        const NetVote &vote = netGame.match().vote;
        const int first = netVoteFirstShown(voteScroll_);
        for (int m = first; m < first + kNetVoteRows; ++m)
        {
            const int slot = m % kNetVoteRows;
            const int map = m < vote.count ? m : -1;
            if (voteSlotMap_[slot] == map)
                continue;
            voteSlotMap_[slot] = map;
            Canvas &canvas = touchCanvas.canvas();
            const int y = voteSlotY(slot);
            canvas.fill(kVoteX, y - 8, kVoteW, kNetVoteRowPixels, kUiBack);
            touchCanvas.listPicture(1, y / 8, 8, 6, nullptr, 12 + slot);
            if (map >= 0)
                canvas.fill(8, y, 64, 48, kUiPanel);
            voteRowAt(map, y);
            netGame.voteSlotChanged(slot);
        }
    }

    void BottomScreen::voteMarker()
    {
        const NetVote &vote = netGame.match().vote;
        Canvas &marker = touchCanvas.marker();
        marker.clear();
        const int last = netVoteLastScroll(vote.count);
        if (last == 0)
            return;
        const int height = marker.height * kNetVoteViewPixels / (vote.count * kNetVoteRowPixels);
        marker.fill(1, 0, 3, marker.height, kUiPanel);
        marker.fill(1, (marker.height - height) * voteScroll_ / last, 3, height, kUiOrange + 2);
    }

    void BottomScreen::voteList()
    {
        votePage_ = VotePage::List;
        stopShown = false;
        touchCanvas.untintAll();
        touchCanvas.listOn();
        shown_ = Shown::Message;
        padPending_ = false;
        // (A bar the list slides under: colour 0 would let it show through.)
        Canvas &heading = touchCanvas.heading();
        heading.fill(0, 0, heading.width, heading.height, kUiPanel);
        const FontView &medium = touchCanvas.font(kFontMedium);
        // (Everyone, or one who chooses alone while the others look on.)
        const NetMatch &match = netGame.match();
        char title[32] = "VOTE: THE NEXT MAP";
        if (match.vote.chooser == match.me)
            std::snprintf(title, sizeof(title), "CHOOSE THE NEXT MAP");
        else if (match.vote.chooser >= 0)
            std::snprintf(title, sizeof(title), "%s CHOOSES", match.final.rows[match.vote.chooser].name);
        drawText(heading, medium, 6, 4, title, inkFor(medium, kUiOrange));
        drawLobbyButton(heading);
        voteClock_ = -1;
        for (int &map : voteSlotMap_)
            map = -2;
        voteSlots();
        touchCanvas.listScroll(voteScroll_ - kVoteTop * 8);
        voteMarker();
    }

    void BottomScreen::showVote()
    {
        voteScroll_ = 0;
        voteSpeed_ = 0;
        voteHeld_ = false;
        voteList();
        netGame.setVoteShown(true);
    }

    void BottomScreen::voteConfirm(int map)
    {
        votePage_ = VotePage::Confirm;
        voteAsked_ = map;
        touchCanvas.listOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll();
        const FontView &medium = touchCanvas.font(kFontMedium);
        drawTextCentred(canvas, medium, 128, 28, "VOTE FOR THIS MAP?", inkFor(medium, kUiOrange));
        canvas.fill(8, voteRowY(1), 64, 48, kUiPanel);
        voteRow(1);
        canvas.button(8, 140, 116, 46, false);
        drawTextCentred(canvas, medium, 66, 140 + (46 - medium.height()) / 2, "YES", inkFor(medium, kUiOrange));
        canvas.button(132, 140, 116, 46, false);
        drawTextCentred(canvas, medium, 190, 140 + (46 - medium.height()) / 2, "NO", inkFor(medium, kUiOrange));
        netGame.voteRedrawn();
    }

    void BottomScreen::showVoteResult()
    {
        votePage_ = VotePage::Result;
        touchCanvas.listOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll();
        const NetVote &vote = netGame.match().vote;
        const FontView &medium = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall);
        drawTextCentred(canvas, medium, 128, 4, "NEXT MAP", inkFor(medium, kUiOrange));
        canvas.fill(8, voteRowY(0), 64, 48, kUiPanel);
        voteRow(0);
        int top[3];
        const int found = vote.top(top, 3);
        char text[40];
        int y = 92;
        for (int n = 0; n < found; ++n, y += 26)
        {
            std::snprintf(text, sizeof(text), "%d. %s", n + 1, vote.names[top[n]]);
            drawText(canvas, medium, 12, y, text, inkFor(medium, n == 0 ? kUiWhite : kUiOrange));
            std::snprintf(text, sizeof(text), "%d", vote.tally[top[n]]);
            drawTextCentred(canvas, medium, 232, y, text, inkFor(medium, n == 0 ? kUiWhite : kUiOrange));
        }
        if (vote.tally[vote.decided] == 0)
            drawTextCentred(canvas, small, 128, 170, "No votes: drawn at random", inkFor(small, kUiGrey));
        netGame.voteRedrawn();
    }

    void BottomScreen::voteUpdate()
    {
        if (votePage_ != VotePage::List)
            return;
        for (int slot = 0; slot < kNetVoteRows; ++slot)
        {
            if (voteSlotMap_[slot] >= 0)
                voteRowAt(voteSlotMap_[slot], voteSlotY(slot));
        }
    }

    void BottomScreen::voteClock(int seconds)
    {
        if (votePage_ != VotePage::List || seconds == voteClock_)
            return;
        voteClock_ = seconds;
        Canvas &canvas = touchCanvas.heading();
        const FontView &small = touchCanvas.font(kFontSmall);
        char text[8];
        std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
        canvas.fill(166, 3, 36, small.height() + 2, kUiPanel);
        drawTextCentred(canvas, small, 184, 5, text, inkFor(small, kUiGrey));
    }

    void BottomScreen::voteSlideTo(int scroll)
    {
        const NetVote &vote = netGame.match().vote;
        scroll = netVoteClampScroll(scroll, vote.count);
        if (scroll == voteScroll_)
            return;
        voteScroll_ = scroll;
        voteSlots();
        touchCanvas.listScroll(voteScroll_ - kVoteTop * 8);
        voteMarker();
    }

    void BottomScreen::voteTouch(bool down, int x, int y)
    {
        const NetMatch &match = netGame.match();
        const NetVote &vote = match.vote;
        if (votePage_ == VotePage::Result)
            return;
        if (down && !voteHeld_)
        {
            voteHeld_ = true;
            voteMoved_ = false;
            voteFromY_ = voteDownY_ = y;
            voteDownX_ = x;
            voteSpeed_ = 0;
            return;
        }
        if (down)
        {
            if (votePage_ != VotePage::List)
                return;
            // The list follows the stylus.
            if (!voteMoved_ && (y - voteDownY_ >= 6 || voteDownY_ - y >= 6))
                voteMoved_ = true;
            if (voteMoved_)
            {
                voteSpeed_ = voteFromY_ - y;
                voteSlideTo(voteScroll_ + voteSpeed_);
            }
            voteFromY_ = y;
            return;
        }
        if (voteHeld_)
        {
            voteHeld_ = false;
            if (voteMoved_)
                return;
            if (votePage_ == VotePage::Confirm)
            {
                if (voteDownY_ >= 140 && voteDownY_ < 186)
                {
                    const bool yes = voteDownX_ >= 8 && voteDownX_ < 124;
                    sound.ui(yes ? kUiSoundPress : kUiSoundTick);
                    if (yes)
                        netGame.vote(voteAsked_);
                    voteList();
                }
                return;
            }
            const int map = netVoteMapAt(voteScroll_, voteDownY_ - kVoteTop * 8, vote.count);
            if (map < 0)
                return;
            // (A vote made is made; and another's choice is theirs: the list
            // is there to look at.)
            if (vote.choice[match.me] >= 0 || (vote.chooser >= 0 && vote.chooser != match.me))
            {
                sound.ui(kUiSoundDeny);
                return;
            }
            sound.ui(kUiSoundPress);
            voteConfirm(map);
            return;
        }
        // Let go: on by what it was moving at, then to rest on a button.
        if (votePage_ != VotePage::List)
            return;
        if (voteSpeed_ > 1 || voteSpeed_ < -1)
        {
            voteSlideTo(voteScroll_ + voteSpeed_);
            voteSpeed_ = voteSpeed_ * 7 / 8;
            return;
        }
        voteSpeed_ = 0;
        const int rest = netVoteRestScroll(voteScroll_, vote.count);
        const int gap = rest - voteScroll_;
        if (gap != 0)
            voteSlideTo(voteScroll_ + (gap > 0 ? (gap + 3) / 4 : (gap - 3) / 4));
    }

    void BottomScreen::voteCard(int row, const std::uint8_t *file, std::size_t size)
    {
        if (row < 0 || row >= kNetVoteRows || previewWidthOf(file, size) != kPreviewSmallWidth)
            return;
        const auto *pixels = reinterpret_cast<const std::uint16_t *>(file + sizeof(PreviewHeader));
        std::uint16_t palette[16];
        previewPalette(pixels, kPreviewSmallWidth * kPreviewSmallHeight, palette);
        Canvas &canvas = touchCanvas.canvas();
        const bool list = votePage_ == VotePage::List;
        const int x = 8, y = list ? voteSlotY(row) : voteRowY(row);
        for (int py = 0; py < kPreviewSmallHeight; ++py)
        {
            for (int px = 0; px < kPreviewSmallWidth; ++px)
                canvas.fill(x + px, y + py, 1, 1, previewNearest(palette, pixels[py * kPreviewSmallWidth + px]));
        }
        if (list)
            touchCanvas.listPicture(1, y / 8, kPreviewSmallWidth / 8, kPreviewSmallHeight / 8, palette, 12 + row);
        else
            touchCanvas.tintWhole(1, y / 8, kPreviewSmallWidth / 8, kPreviewSmallHeight / 8, palette, 12 + row);
    }

    void BottomScreen::showScores()
    {
        stopShown = false;
        touchCanvas.listOff();
        touchCanvas.pageOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle);
        char said[32];
        netFinalTitle(netGame.match(), said, sizeof(said));
        drawTextCentred(canvas, title, 128, 6, said, inkFor(title, kUiOrange));
        drawFinal(6 + title.height() + 2);
        drawLobbyButton(canvas);
        touchCanvas.pageOn();
    }

    void BottomScreen::showProgress(const char *title, const char *step, const char *note, int done, int total,
                                    bool stop, bool percent)
    {
        create();
        touchCanvas.pageOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &big = touchCanvas.font(kFontTitle), &medium = touchCanvas.font(kFontMedium),
                       &small = touchCanvas.font(kFontSmall);
        drawTextCentred(canvas, big, 128, 22, title, inkFor(big, kUiOrange));
        char cut[64];
        std::snprintf(cut, sizeof(cut), "%.*s", textFit(medium, step, 240), step);
        drawTextCentred(canvas, medium, 128, 70, cut, inkFor(medium, kUiWhite));
        if (note != nullptr && note[0] != '\0')
        {
            std::snprintf(cut, sizeof(cut), "%.*s", textFit(small, note, 240), note);
            drawTextCentred(canvas, small, 128, 70 + medium.height() + 4, cut, inkFor(small, kUiGrey));
        }
        // How far: a bar, and in words under it.
        constexpr int kBarX = 28, kBarY = 108, kBarW = 200, kBarH = 10;
        if (total > 0)
        {
            canvas.fill(kBarX, kBarY, kBarW, kBarH, kUiTrack);
            canvas.frame(kBarX - 1, kBarY - 1, kBarW + 2, kBarH + 2, kUiEdge);
            const int filled = kBarW * (done < total ? done : total) / total;
            canvas.fill(kBarX, kBarY, filled, kBarH, kUiOrange + 1);
            if (percent)
                std::snprintf(cut, sizeof(cut), "%d%%", done * 100 / total);
            else
                std::snprintf(cut, sizeof(cut), "%d of %d", done, total);
            drawTextCentred(canvas, small, 128, kBarY + kBarH + 6, cut, inkFor(small, kUiGrey));
        }
        if (stop)
        {
            canvas.button(kStopX, kStopY, kStopW, kStopH, stopHit);
            drawTextCentred(canvas, medium, kStopX + kStopW / 2, kStopY + (kStopH - medium.height()) / 2,
                            stopHit ? "STOPPING" : "STOP", inkFor(medium, stopHit ? kUiWhite : kUiOrange));
        }
        stopShown = stop;
        touchCanvas.pageOn();
    }

    bool BottomScreen::stopTouched()
    {
        const bool hit = stopHit;
        if (hit)
            stopShown = stopHit = false;
        return hit;
    }

    void BottomScreen::stopDone() { stopShown = stopHit = false; }

    void BottomScreen::onVblank()
    {
        if (!stopShown || stopHit || !(keysCurrent() & KEY_TOUCH))
            return;
        touchPosition touch;
        touchRead(&touch);
        stopHit = touch.px >= kStopX && touch.px < kStopX + kStopW && touch.py >= kStopY && touch.py < kStopY + kStopH;
    }

    void BottomScreen::showFatal(const char *line1, const char *line2, const char *line3)
    {
        stopShown = false;
        create();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Nothing;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle), &font = touchCanvas.font(kFontSmall);
        drawTextCentred(canvas, title, 128, 20, "Half-Life DS", inkFor(title, kUiOrange));
        int y = 64;
        for (const char *line : {line1, line2, line3})
        {
            // Each on as many rows as it takes.
            while (line != nullptr && *line != '\0' && y < 180)
            {
                const int fits = textFit(font, line, 244);
                drawText(canvas, font, 6, y, line, inkFor(font, kUiWhite), fits);
                line += fits;
                while (*line == ' ')
                    ++line;
                y += font.height();
            }
            y += 6;
        }
    }

    void BottomScreen::readout(int row, const char *text)
    {
        if (!settings.debugReadout || shown_ == Shown::Nothing || row < 0 || row > 8)
            return;
        Canvas &canvas = touchCanvas.canvas();
        canvas.fill(0, row * 8, 256, 8, kUiBack);
        drawText(canvas, touchCanvas.plain(), 0, row * 8, text, inkPlain(kUiWhite + 2), 32);
    }
} // namespace hlrt
