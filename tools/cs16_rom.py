#!/usr/bin/env python3
"""A Counter-Strike ROM that carries its game in itself.

After ./make.sh: the same program as half-life-nds-port.nds, with its own
name, game code and icon, and the player's converted Counter-Strike (the
converter's cstrike folder: common.npac, manifest.bin, maps/) put in its
NitroFS under data/cstrike/. The program finds it there at start
(Global::checkRomData) and reads nothing else from the card; what it writes
(settings, saves) goes to _nds/cs16_ds/ on the card, if there is one.

The game in it is made from the player's own copy, as on a card: a ROM made
so is the player's own, and is not to be shared -- nor anything of it put in
the repository.

    python tools/cs16_rom.py --data local/cs16/cstrike [--icon local/cs16/game.tga]
                             [--out cs16-ds.nds] [--emu build/emu/cs16-ds.nds]

--icon: a picture to make the 32x32, 16-colour banner icon from (the
player's own game's icon, say: cstrike/game.tga); none, the repo's Logo.bmp.
--emu: a copy marked as homebrew (game code ####), for melonDS's DS mode to
give it the SD card.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write_icon(source, out_path):
    """`source` as a 32x32 picture of 16 colours, as ndstool takes one: a
    4-bit BMP, colour 0 the one see-through (here: black, the background
    put under what is see-through in the source)."""
    from PIL import Image

    image = Image.open(source)
    # (An .ico holds several: the biggest.)
    if getattr(image, 'n_frames', 1) > 1 or image.format == 'ICO':
        try:
            sizes = sorted(image.info.get('sizes', []), key=lambda s: s[0] * s[1])
            if sizes:
                image.size = sizes[-1]
        except Exception:
            pass
    image = image.convert('RGBA')
    background = Image.new('RGBA', image.size, (0, 0, 0, 255))
    background.alpha_composite(image)
    small = background.convert('RGB').resize((32, 32), Image.LANCZOS)
    # 15 colours of its own, and colour 0 kept for see-through (unused).
    quantised = small.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    palette = (quantised.getpalette() or [])[:15 * 3]
    palette += [0] * (15 * 3 - len(palette))
    colours = [(0, 0, 0)] + [tuple(palette[i * 3:i * 3 + 3]) for i in range(15)]
    pixels = [p + 1 for p in quantised.getdata()]

    rows = []
    for y in range(31, -1, -1):  # (a BMP's rows go up from the bottom)
        row = bytearray()
        for x in range(0, 32, 2):
            row.append((pixels[y * 32 + x] << 4) | pixels[y * 32 + x + 1])
        rows.append(bytes(row))
    data = b''.join(rows)
    palette_bytes = b''.join(struct.pack('<BBBB', b, g, r, 0) for (r, g, b) in colours)
    offset = 14 + 40 + len(palette_bytes)
    header = struct.pack('<2sIHHI', b'BM', offset + len(data), 0, 0, offset)
    info = struct.pack('<IiiHHIIiiII', 40, 32, 32, 1, 4, 0, len(data), 2835, 2835, 16, 16)
    with open(out_path, 'wb') as f:
        f.write(header + info + palette_bytes + data)


def icon_of(source):
    """`source` as the banner's icon: 32x32 at 4 bits, in 8x8 tiles, and its
    16 colours (BGR555), colour 0 see-through -- as write_icon makes it."""
    from PIL import Image

    image = Image.open(source).convert('RGBA')
    background = Image.new('RGBA', image.size, (0, 0, 0, 255))
    background.alpha_composite(image)
    small = background.convert('RGB').resize((32, 32), Image.LANCZOS)
    quantised = small.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    palette = (quantised.getpalette() or [])[:15 * 3]
    palette += [0] * (15 * 3 - len(palette))
    colours = [(0, 0, 0)] + [tuple(palette[i * 3:i * 3 + 3]) for i in range(15)]
    pixels = [p + 1 for p in quantised.getdata()]
    tiles = bytearray()
    for ty in range(4):
        for tx in range(4):
            for y in range(8):
                for x in range(0, 8, 2):
                    a = pixels[(ty * 8 + y) * 32 + tx * 8 + x]
                    b = pixels[(ty * 8 + y) * 32 + tx * 8 + x + 1]
                    tiles.append(a | b << 4)
    pal = b''.join(struct.pack('<H', (r >> 3) | (g >> 3) << 5 | (bb >> 3) << 10) for (r, g, bb) in colours)
    return bytes(tiles), pal


def crc16(data):
    c = 0xFFFF
    for x in data:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def name_child(path, title, icon=None):
    """The program sent by Download Play (dlplay/child.nds: the same as the
    Half-Life ROM's), named as this game in its banner -- what a console
    being sent it shows -- with this game's icon, if there is one."""
    rom = bytearray(open(path, 'rb').read())
    at = struct.unpack_from('<I', rom, 0x68)[0]
    if at == 0 or at + 0x840 > len(rom):
        return
    version = struct.unpack_from('<H', rom, at)[0]
    if icon is not None:
        tiles, pal = icon_of(icon)
        rom[at + 0x20:at + 0x220] = tiles
        rom[at + 0x220:at + 0x240] = pal
    languages = 6 + (1 if version >= 2 else 0) + (1 if version >= 3 else 0)
    text = title.encode('utf-16-le')[:0xFE]
    for lang in range(languages):
        slot = at + 0x240 + lang * 0x100
        if slot + 0x100 > len(rom):
            break
        rom[slot:slot + 0x100] = text + bytes(0x100 - len(text))
    struct.pack_into('<H', rom, at + 2, crc16(rom[at + 0x20:at + 0x840]))
    if version >= 2 and at + 0x940 <= len(rom):
        struct.pack_into('<H', rom, at + 4, crc16(rom[at + 0x20:at + 0x940]))
    if version >= 3 and at + 0xA40 <= len(rom):
        struct.pack_into('<H', rom, at + 6, crc16(rom[at + 0x20:at + 0xA40]))
    open(path, 'wb').write(rom)


def copy_game(data, into):
    """The converter's cstrike folder, what the game reads of it: the
    archives, the manifest, the music -- not anything written to it."""
    os.makedirs(into, exist_ok=True)
    for name in ('common.npac', 'manifest.bin'):
        src = os.path.join(data, name)
        if not os.path.isfile(src):
            raise SystemExit(f'cs16_rom: no {name} in {data} (the converted cstrike folder)')
        shutil.copy2(src, os.path.join(into, name))
    # (air/: what a console with no card is sent by Download Play.)
    for folder in ('maps', 'music', 'air'):
        src = os.path.join(data, folder)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(into, folder))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--data', required=True, help='the converted cstrike folder')
    parser.add_argument('--icon', help='a picture to make the icon from')
    parser.add_argument('--out', default='cs16-ds.nds')
    parser.add_argument('--emu', help='a copy with game code #### (melonDS, DS mode)')
    parser.add_argument('--title', default='Counter-Strike 1.6 DS')
    parser.add_argument('--subtitle', default='xRex1984')
    parser.add_argument('--author', default='Based on CrowbarDS HL1 Port')
    args = parser.parse_args()

    os.chdir(ROOT)
    for need in ('build/nitrofs', 'build/arm9/arm9.elf', 'build/arm7/arm7.elf'):
        if not os.path.exists(need):
            raise SystemExit(f'cs16_rom: {need} is not there: run ./make.sh first')
    # (make.sh's BlocksDS, where this is run on its own.)
    roots = [os.environ.get('BLOCKSDS', ''), os.environ.get('HLDS_BLOCKSDS', ''), '/opt/blocksds/hl-known-good',
             '/opt/wonderful/thirdparty/blocksds/core', '/opt/blocksds/core']
    tools = [os.path.join(r, 'tools', 'ndstool', 'ndstool') for r in roots if r]
    ndstool = next((t for t in tools if os.path.isfile(t)), None)
    if ndstool is None:
        raise SystemExit('cs16_rom: no ndstool found (BLOCKSDS)')

    # The ROM's own files, and the game beside them.
    fs = os.path.join('build', 'cs16-nitrofs')
    if os.path.isdir(fs):
        shutil.rmtree(fs)
    shutil.copytree('build/nitrofs', fs)
    copy_game(args.data, os.path.join(fs, 'data', 'cstrike'))
    # (What a console sent the game by Download Play is told it is.)
    child = os.path.join(fs, 'dlplay', 'child.nds')
    if os.path.isfile(child):
        name_child(child, args.title + '\nSent by Download Play\n' + args.subtitle, args.icon)

    icon = 'Logo.bmp'
    if args.icon:
        icon = os.path.join('build', 'cs16-icon.bmp')
        write_icon(args.icon, icon)

    def make(out, code):
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        command = [ndstool, '-c', out, '-7', 'build/arm7/arm7.elf', '-9', 'build/arm9/arm9.elf', '-b', icon,
                   f'{args.title};{args.subtitle};{args.author}', '-d', fs, '-uc', '2',
                   '-g', code, '00', 'HOMEBREW', str(0x57)]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        size = os.path.getsize(out) / (1024 * 1024)
        print(f'cs16_rom: {out} ({size:.1f} MB)')

    # (The game code's W, as the Half-Life ROM's: TWiLight Menu++ offers a
    # 3DS's wide screen for it.)
    make(args.out, 'WCSD')
    if args.emu:
        make(args.emu, '####')


if __name__ == '__main__':
    sys.exit(main())
