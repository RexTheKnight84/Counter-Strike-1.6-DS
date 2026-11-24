#pragma once

// The DS's cooked data, read by the 3DS (ctr/): the same files on the same
// card, put into the shapes the 3DS's GPU (the PICA200) takes. Nothing new is
// cooked for it; a level, its textures and its models are what the DS ROM
// reads, decoded as they are loaded.
//
// GEOMETRY. A level's surface (HlLevel.hpp's Surface) is a run of the DS
// geometry engine's packed commands: BEGIN_VTXS, COLOR, TEXCOORD and the six
// vertex forms. Here they become a plain list of triangles, a vertex each
// with its position (quarter units, as sent), its texture coordinate (texels
// in 12.4, as sent) and its colour (RGB555 widened to bytes) -- attributes
// the PICA reads as they are, shorts and bytes, so a level's geometry costs
// the 3DS what it costs the DS. Quads and strips are cut into triangles in
// the order the DS draws them: a strip's odd triangles turned round, a quad
// strip's quads as v0 v1 v3 v2.
//
// TEXTURES. A .htx (HlTexture.hpp) is in one of the DS's formats: 4x4
// compressed, A3I5, A5I3, 256 colours. Each is widened to RGBA8 texel for
// texel as the DS's hardware samples it -- 5 bits of colour made 8 by a shift,
// three bits of alpha made five as the hardware does (a*4 + a/2), a 4x4
// block's mode-made colours averaged as it averages them -- and laid out in
// the PICA's order (8 by 8 tiles in Z order, the picture upside down), or
// left in rows for the tests.
//
// std-only and inline, no heap: the host tests check it against the
// converter's own decoders (tests/test_ctrdecode.cpp).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HlTexture.hpp"

namespace hldata::ctr
{
    // ------------------------------------------------------------- geometry

    struct Vertex
    {
        std::int16_t x, y, z; ///< quarter units
        std::int16_t s, t;    ///< texels, 12.4
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(Vertex) == 14);

    inline constexpr std::uint8_t kGxColor = 0x20, kGxTexCoord = 0x22, kGxVtx16 = 0x23, kGxVtx10 = 0x24,
                                  kGxVtxXY = 0x25, kGxVtxXZ = 0x26, kGxVtxYZ = 0x27, kGxVtxDiff = 0x28,
                                  kGxBeginVtxs = 0x40;

    /// RGB555 as bytes: five bits made eight by a shift, as the converter's
    /// decoder (and a screen capture of the DS) has it.
    inline void widen555(std::uint16_t c, std::uint8_t &r, std::uint8_t &g, std::uint8_t &b)
    {
        r = static_cast<std::uint8_t>((c & 31) << 3);
        g = static_cast<std::uint8_t>(((c >> 5) & 31) << 3);
        b = static_cast<std::uint8_t>(((c >> 10) & 31) << 3);
    }

    /// The triangles in `count` packed command words, into `out` (room for
    /// `most`): how many vertices were written -- three a triangle -- or -1
    /// for a stream that is not the converter's, or does not fit.
    inline int gxTriangles(const std::uint32_t *words, std::size_t count, Vertex *out, int most)
    {
        auto sx10 = [](std::uint32_t v) { return static_cast<int>((v & 0x3FF) ^ 0x200) - 0x200; };
        Vertex pending{};
        pending.a = 255;
        bool haveColour = false, havePrevious = false, haveRun = false;
        int type = 0;
        Vertex ring[4]{}; // the run's last vertices, as its primitive needs them
        int inRun = 0;
        int written = 0;
        auto emit = [&](const Vertex &a, const Vertex &b, const Vertex &c) {
            if (written + 3 > most)
                return false;
            out[written++] = a;
            out[written++] = b;
            out[written++] = c;
            return true;
        };
        auto vertex = [&](const Vertex &v) {
            switch (type)
            {
            case 0: // triangles
                ring[inRun % 3] = v;
                if (++inRun % 3 == 0)
                    return emit(ring[0], ring[1], ring[2]);
                return true;
            case 1: // quads: (0 1 2) (0 2 3)
                ring[inRun % 4] = v;
                if (++inRun % 4 == 0)
                    return emit(ring[0], ring[1], ring[2]) && emit(ring[0], ring[2], ring[3]);
                return true;
            case 2: // a strip: each new vertex and the two before it, the odd turned round
                ring[0] = ring[1];
                ring[1] = ring[2];
                ring[2] = v;
                if (++inRun >= 3)
                    return (inRun & 1) ? emit(ring[0], ring[1], ring[2]) : emit(ring[1], ring[0], ring[2]);
                return true;
            default: // a quad strip: each pair after the first, a quad v0 v1 v3 v2
                ring[0] = ring[1];
                ring[1] = ring[2];
                ring[2] = ring[3];
                ring[3] = v;
                if (++inRun >= 4 && (inRun & 1) == 0)
                    return emit(ring[0], ring[1], ring[3]) && emit(ring[0], ring[3], ring[2]);
                return true;
            }
        };

        std::size_t pos = 0;
        while (pos < count)
        {
            const std::uint32_t header = words[pos++];
            if (header == 0)
                return -1;
            for (int k = 0; k < 4; ++k)
            {
                const auto code = static_cast<std::uint8_t>(header >> (8 * k));
                if (code == 0)
                    break;
                const int params = code == kGxVtx16 ? 2 : 1;
                if (pos + params > count)
                    return -1;
                const std::uint32_t p0 = words[pos], p1 = params == 2 ? words[pos + 1] : 0;
                pos += params;
                if (code == kGxBeginVtxs)
                {
                    if (p0 > 3)
                        return -1;
                    type = static_cast<int>(p0);
                    inRun = 0;
                    haveRun = true;
                    continue;
                }
                if (code == kGxColor)
                {
                    widen555(static_cast<std::uint16_t>(p0), pending.r, pending.g, pending.b);
                    haveColour = true;
                    continue;
                }
                if (code == kGxTexCoord)
                {
                    pending.s = static_cast<std::int16_t>(p0 & 0xFFFF);
                    pending.t = static_cast<std::int16_t>(p0 >> 16);
                    continue;
                }
                if (code < kGxVtx16 || code > kGxVtxDiff || !haveRun || !haveColour)
                    return -1;
                if (code != kGxVtx16 && code != kGxVtx10 && !havePrevious)
                    return -1;
                const auto lo = static_cast<std::int16_t>(p0 & 0xFFFF), hi = static_cast<std::int16_t>(p0 >> 16);
                switch (code)
                {
                case kGxVtx16:
                    pending.x = lo;
                    pending.y = hi;
                    pending.z = static_cast<std::int16_t>(p1 & 0xFFFF);
                    break;
                case kGxVtx10:
                    pending.x = static_cast<std::int16_t>(sx10(p0) * 64);
                    pending.y = static_cast<std::int16_t>(sx10(p0 >> 10) * 64);
                    pending.z = static_cast<std::int16_t>(sx10(p0 >> 20) * 64);
                    break;
                case kGxVtxXY: pending.x = lo; pending.y = hi; break;
                case kGxVtxXZ: pending.x = lo; pending.z = hi; break;
                case kGxVtxYZ: pending.y = lo; pending.z = hi; break;
                default:
                    pending.x = static_cast<std::int16_t>(pending.x + sx10(p0));
                    pending.y = static_cast<std::int16_t>(pending.y + sx10(p0 >> 10));
                    pending.z = static_cast<std::int16_t>(pending.z + sx10(p0 >> 20));
                    break;
                }
                havePrevious = true;
                if (!vertex(pending))
                    return -1;
            }
        }
        return written;
    }

    // ------------------------------------------------------------- textures

    /// Where texel (x, y) of a `width`-wide texture lies in the PICA's order:
    /// rows of 8 by 8 tiles from the picture's BOTTOM, each tile's 64 in Z
    /// order (x and y's bits interleaved, x's lowest).
    inline std::uint32_t picaOffset(int x, int y, int width, int height)
    {
        const int fy = height - 1 - y;
        const int tile = (fy >> 3) * (width >> 3) + (x >> 3);
        const int ix = x & 7, iy = fy & 7;
        const int z = (ix & 1) | ((iy & 1) << 1) | ((ix & 2) << 1) | ((iy & 2) << 2) | ((ix & 4) << 2) | ((iy & 4) << 3);
        return static_cast<std::uint32_t>(tile * 64 + z);
    }

    /// A texel as the PICA's RGBA8 has it: 0xRRGGBBAA.
    inline constexpr std::uint32_t rgba8(int r, int g, int b, int a)
    {
        return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
               (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
    }

    /// A .htx's pictures (`body`: what follows its header, as
    /// textureHeaderValid() has checked) widened into `out` (width * height
    /// texels, 0xRRGGBBAA). `pica`: in the PICA's order; else in rows from the
    /// top. False: a texel points past its palette.
    inline bool decodeTexture(const TextureHeader &h, const std::uint8_t *body, std::uint32_t *out, bool pica)
    {
        const int w = h.width, ht = h.height;
        const std::uint8_t *texels = body;
        const std::uint8_t *index = body + h.texelBytes;
        const std::uint8_t *palette = index + h.indexBytes;
        const int colours = h.paletteColours;
        auto colourOf = [&](int i) {
            return static_cast<std::uint16_t>(palette[i * 2] | (palette[i * 2 + 1] << 8));
        };
        auto put = [&](int x, int y, std::uint32_t v) {
            out[pica ? picaOffset(x, y, w, ht) : static_cast<std::uint32_t>(y * w + x)] = v;
        };

        if (h.flags & (kTextureA3I5 | kTexturePal256))
        {
            const bool a5i3 = (h.flags & kTextureA5I3) != 0, pal256 = (h.flags & kTexturePal256) != 0;
            for (int y = 0; y < ht; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const std::uint8_t t = texels[y * w + x];
                    const int c = pal256 ? t : (a5i3 ? t & 7 : t & 31);
                    if (c >= colours)
                        return false;
                    std::uint8_t r, g, b;
                    widen555(colourOf(c), r, g, b);
                    const int a3 = t >> 5;
                    const int a5 = pal256 ? 31 : (a5i3 ? t >> 3 : a3 * 4 + a3 / 2);
                    put(x, y, rgba8(r, g, b, a5 * 255 / 31));
                }
            }
            return true;
        }

        // 4x4 compressed (GBATEK, "DS 3D Texture Formats").
        const int across = w / 4;
        for (int blk = 0; blk < across * (ht / 4); ++blk)
        {
            std::uint32_t bits;
            std::memcpy(&bits, texels + blk * 4, 4);
            const std::uint16_t idx = static_cast<std::uint16_t>(index[blk * 2] | (index[blk * 2 + 1] << 8));
            const int first = (idx & 0x3FFF) * 2, mode = idx >> 14;
            const int needed = mode == 2 ? 4 : (mode == 0 ? 3 : 2);
            if (first + needed > colours)
                return false;
            int c[4][3] = {};
            std::uint8_t r, g, b;
            for (int k = 0; k < (mode == 2 ? 4 : (mode == 0 ? 3 : 2)); ++k)
            {
                widen555(colourOf(first + k), r, g, b);
                c[k][0] = r, c[k][1] = g, c[k][2] = b;
            }
            if (mode == 1)
                for (int k = 0; k < 3; ++k)
                    c[2][k] = (c[0][k] + c[1][k]) / 2;
            if (mode == 3)
                for (int k = 0; k < 3; ++k)
                {
                    c[2][k] = (c[0][k] * 5 + c[1][k] * 3) / 8;
                    c[3][k] = (c[0][k] * 3 + c[1][k] * 5) / 8;
                }
            const bool clear3 = mode < 2;
            const int bx = (blk % across) * 4, by = (blk / across) * 4;
            for (int t = 0; t < 16; ++t)
            {
                const int v = (bits >> (t * 2)) & 3;
                put(bx + (t & 3), by + (t >> 2), v == 3 && clear3 ? 0u : rgba8(c[v][0], c[v][1], c[v][2], 255));
            }
        }
        return true;
    }

    /// The texture coordinate's scale to the PICA's 0..1: a 12.4 texel count
    /// over the texture's size.
    inline float texScale(int size) { return 1.0f / (16.0f * static_cast<float>(size)); }
} // namespace hldata::ctr
