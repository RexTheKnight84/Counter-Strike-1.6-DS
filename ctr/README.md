# Native 3DS program

Counter-Strike from the DS port, built as a native 3DS program (a `.3dsx`) for
the old 3DS: 268 MHz ARM11, 64 MB for an application, the PICA200 GPU, a
400 by 240 top screen and a 320 by 240 touch screen. It runs the same game code
as the DS ROM. `shared/` is compiled in unchanged, and this folder replaces
`source/` and `arm7/`.

```sh
dkp-pacman -S 3ds-dev        # devkitARM, libctru, citro3d, citro2d
make -C ctr                  # ctr/hl_ctr.3dsx
```

It reads the DS port's data where the converter puts it: `sd:/_nds/hl_nds/data/cstrike/` (`maps/*.npac` and `common.npac`).

## Controls

They use the DS port's layouts (`shared/HlControls.hpp`): classic, modern,
buttons only and custom, for either hand. `shared/HlCtrControls.hpp` adapts
the 3DS hardware to them:

| 3DS | Does |
|---|---|
| A B X Y, D-pad, L R, Start, Select | What they do on the DS, in every layout (they use the same key bits) |
| Circle Pad | The D-pad's work, in eight directions like WASD. A full tilt runs. A partial tilt walks (the PC's walk key), unless that is turned off |
| Touch screen | The DS touch layout (`HlTouchLayout.hpp`), drawn a quarter larger. Touches are scaled back by 4/5, and read finer than a DS pixel |
| C-stick (New 3DS, or Circle Pad Pro) | Looks around, like a mouse. Uses the same aim speed as the stylus |
| ZL / ZR (same) | Crouch / jump by default (the PC's Ctrl and Space); either can take any action |

An old 3DS without the Circle Pad Pro loses nothing the DS layouts had.

## Closer to the PC game

`shared/HlCsPc.hpp` adds the CS 1.6 rules that the DS's CS mode (`HlCs.hpp`)
covers with Half-Life stand-ins:

- the top running speed for each weapon, and with its scope up
- damage falloff with range, from each gun's range modifier
- hit locations: head ×4, stomach ×1.25, legs ×0.75
- armour: kevlar covers the body, a helmet the head, nothing the legs; how much damage gets through depends on each gun's armour ratio
- wall penetration counts
- CS's movement constants (accelerate 5, stop speed 75, walk at 0.52)

## Order of work

1. **Controls.** Done (`HlCtrControls.hpp`).
2. **The world.** Done: `source/World.cpp` reads a map straight from the DS data on the SD card (`HlCtrNpac.hpp`) and draws it whole with citro3d (`world.v.pica`). The DS's packed geometry commands become triangle lists, and its 4x4-compressed, A3I5, A5I3 and 256-colour textures become RGBA8 (`HlCtrDecode.hpp`). No new converter output is needed. `source/main.cpp` loads de_dust2, de_inferno or de_nuke at a CT spawn and lets you fly round them with the DS layouts.
3. **Platform layer.** Replace `source/platform` and `source/audio` (ndsp instead of the ARM7 mixer and the DSi DSP).
4. **The rest of the drawing.** The sky box, brush entities under their own movement, animated and scrolling textures, then models (`.hmd`: the same approach as the level), effects and the HUD. Add the leaves' PVS for the bigger maps.
5. **Use the extra room.** Put `HlCsPc.hpp` into play (player speed, `HlCsBots` damage), raise `kBotSlotsPerSide` as far as the old 3DS's CPU allows at 30 fps, and give each CS gun its own firing behaviour instead of the Half-Life stand-in.
