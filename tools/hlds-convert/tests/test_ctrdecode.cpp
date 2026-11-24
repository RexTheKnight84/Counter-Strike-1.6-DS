// The DS's cooked data as the 3DS reads it (shared/HlCtrDecode.hpp): the same
// triangles and the same texels as the converter's own decoders give.

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "HlCtrDecode.hpp"
#include "HlCtrNpac.hpp"
#include "HlLevelReader.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "TestBspBuilder.hpp"
#include "hlds/GxStream.hpp"
#include "hlds/TextureEncoder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    bool same(const ctr::Vertex &a, const hlds::GxVertex &b)
    {
        std::uint8_t r, g, bl;
        ctr::widen555(b.colour, r, g, bl);
        return a.x == b.x && a.y == b.y && a.z == b.z && a.s == b.s && a.t == b.t && a.r == r && a.g == g &&
               a.b == bl && a.a == 255;
    }

    /// The converter's runs as triangles, by the DS's rules for each primitive.
    std::vector<hlds::GxVertex> triangles(const std::vector<hlds::GxRun> &runs)
    {
        std::vector<hlds::GxVertex> t;
        for (const hlds::GxRun &run : runs)
        {
            const auto &v = run.vertices;
            const int n = static_cast<int>(v.size());
            switch (run.type)
            {
            case hlds::GxPrimitive::Triangles:
                for (int i = 0; i + 2 < n; i += 3)
                    t.insert(t.end(), {v[i], v[i + 1], v[i + 2]});
                break;
            case hlds::GxPrimitive::Quads:
                for (int i = 0; i + 3 < n; i += 4)
                    t.insert(t.end(), {v[i], v[i + 1], v[i + 2], v[i], v[i + 2], v[i + 3]});
                break;
            case hlds::GxPrimitive::TriangleStrip:
                for (int i = 0; i + 2 < n; ++i)
                {
                    if (i & 1)
                        t.insert(t.end(), {v[i + 1], v[i], v[i + 2]});
                    else
                        t.insert(t.end(), {v[i], v[i + 1], v[i + 2]});
                }
                break;
            case hlds::GxPrimitive::QuadStrip:
                for (int i = 0; i + 3 < n; i += 2)
                    t.insert(t.end(), {v[i], v[i + 1], v[i + 3], v[i], v[i + 3], v[i + 2]});
                break;
            }
        }
        return t;
    }
} // namespace

int main()
{
    std::mt19937 rng(1234);
    auto coord = [&](int prev) {
        switch (rng() % 4)
        {
        case 0: return prev;                                            // (an axis left alone: XY, XZ, YZ)
        case 1: return static_cast<int>(rng() % 1000) - 500 + prev / 2; // (near: VTX_DIFF)
        case 2: return (static_cast<int>(rng() % 1000) - 500) * 64;     // (VTX_10)
        default: return static_cast<int>(rng() % 60000) - 30000;        // (VTX_16)
        }
    };

    // Geometry: streams packed by the converter, every primitive and every
    // vertex form.
    int streams = 0, mismatched = 0;
    for (int round = 0; round < 400; ++round)
    {
        hlds::GxPacker pack;
        hlds::GxVertex v;
        for (int runs = 1 + rng() % 3; runs > 0; --runs)
        {
            const auto type = static_cast<hlds::GxPrimitive>(rng() % 4);
            pack.begin(type);
            const int n = type == hlds::GxPrimitive::Triangles ? 3 * (1 + rng() % 4)
                          : type == hlds::GxPrimitive::Quads   ? 4 * (1 + rng() % 3)
                          : type == hlds::GxPrimitive::QuadStrip ? 4 + 2 * (rng() % 4)
                                                                 : 3 + rng() % 6;
            for (int i = 0; i < n; ++i)
            {
                auto clamp = [](int c) { return static_cast<std::int16_t>(c < -32768 ? -32768 : (c > 32767 ? 32767 : c)); };
                v.x = clamp(coord(v.x));
                v.y = clamp(coord(v.y));
                v.z = clamp(coord(v.z));
                v.s = static_cast<std::int16_t>(rng());
                v.t = static_cast<std::int16_t>(rng());
                if (rng() % 3 == 0)
                    v.colour = static_cast<std::uint16_t>(rng() & 0x7FFF);
                pack.vertex(v);
            }
        }
        const std::vector<std::uint32_t> words = pack.finish();
        std::vector<hlds::GxRun> runs;
        std::string error;
        if (!hlds::decodeGxStream(words.data(), words.size(), runs, error))
            continue;
        ++streams;
        const std::vector<hlds::GxVertex> want = triangles(runs);
        std::vector<ctr::Vertex> got(want.size() + 3);
        const int n = ctr::gxTriangles(words.data(), words.size(), got.data(), static_cast<int>(got.size()));
        bool ok = n == static_cast<int>(want.size());
        for (int i = 0; ok && i < n; ++i)
            ok = same(got[i], want[i]);
        mismatched += ok ? 0 : 1;
    }
    check(streams > 300, "the converter packed and read back the streams");
    check(mismatched == 0, "every stream gives the converter's triangles, vertex for vertex");

    {
        // A strip's order by hand: 0 1 2, then 2 1 3.
        hlds::GxPacker pack;
        pack.begin(hlds::GxPrimitive::TriangleStrip);
        for (int i = 0; i < 4; ++i)
        {
            hlds::GxVertex v;
            v.x = static_cast<std::int16_t>(i * 64);
            pack.vertex(v);
        }
        const auto words = pack.finish();
        ctr::Vertex out[12];
        const int n = ctr::gxTriangles(words.data(), words.size(), out, 12);
        check(n == 6 && out[3].x == 128 && out[4].x == 64 && out[5].x == 192,
              "a strip's second triangle is turned round, as the DS draws it");
        check(ctr::gxTriangles(words.data(), words.size(), out, 5) == -1, "a stream that does not fit is refused");
    }
    {
        const std::uint32_t bad[] = {0x23u, 0, 0};
        ctr::Vertex out[3];
        check(ctr::gxTriangles(bad, 3, out, 3) == -1, "a vertex before BEGIN_VTXS is refused");
    }

    // Textures: random data in each format, decoded both ways.
    auto texture = [&](hlds::TextureFormat format, int w, int h, int colours, std::uint32_t flags) {
        hlds::CookedTexture tex;
        tex.width = w;
        tex.height = h;
        tex.format = format;
        tex.palette.resize(colours * 2);
        for (auto &b : tex.palette)
            b = static_cast<std::uint8_t>(rng());
        for (int c = 0; c < colours; ++c)
            tex.palette[c * 2 + 1] &= 0x7F;
        if (format == hlds::TextureFormat::Tex4x4)
        {
            tex.texels.resize(w * h / 4);
            tex.index.resize(w * h / 8);
            for (auto &b : tex.texels)
                b = static_cast<std::uint8_t>(rng());
            for (int blk = 0; blk < w * h / 16; ++blk)
            {
                const int mode = rng() % 4;
                const int pair = static_cast<int>(rng() % ((colours - 4) / 2));
                const std::uint16_t idx = static_cast<std::uint16_t>(pair | (mode << 14));
                tex.index[blk * 2] = static_cast<std::uint8_t>(idx);
                tex.index[blk * 2 + 1] = static_cast<std::uint8_t>(idx >> 8);
            }
        }
        else
        {
            tex.texels.resize(w * h);
            for (auto &b : tex.texels)
            {
                b = static_cast<std::uint8_t>(rng());
                if (format == hlds::TextureFormat::A3I5)
                    b = static_cast<std::uint8_t>((b & 0xE0) | ((b & 31) % colours));
            }
        }
        hlds::RgbaImage want;
        std::string error;
        if (!hlds::decodeTexture(tex, want, error))
            return false;
        TextureHeader hd{};
        hd.width = static_cast<std::uint16_t>(w);
        hd.height = static_cast<std::uint16_t>(h);
        hd.paletteColours = static_cast<std::uint16_t>(colours);
        hd.texelBytes = static_cast<std::uint32_t>(tex.texels.size());
        hd.indexBytes = static_cast<std::uint32_t>(tex.index.size());
        hd.flags = flags;
        std::vector<std::uint8_t> body(tex.texels);
        body.insert(body.end(), tex.index.begin(), tex.index.end());
        body.insert(body.end(), tex.palette.begin(), tex.palette.end());
        std::vector<std::uint32_t> rows(w * h), tiled(w * h);
        if (!ctr::decodeTexture(hd, body.data(), rows.data(), false) ||
            !ctr::decodeTexture(hd, body.data(), tiled.data(), true))
            return false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const std::uint8_t *p = want.at(x, y);
                // (The converter leaves a clear texel black; so does this.)
                const std::uint32_t expect = p[3] == 0 && format == hlds::TextureFormat::Tex4x4
                                                 ? 0u
                                                 : ctr::rgba8(p[0], p[1], p[2], p[3]);
                if (rows[y * w + x] != expect || tiled[ctr::picaOffset(x, y, w, h)] != expect)
                    return false;
            }
        return true;
    };
    check(texture(hlds::TextureFormat::Tex4x4, 64, 32, 64, 0), "4x4 compressed: every texel as the converter's");
    check(texture(hlds::TextureFormat::A3I5, 32, 16, 32, kTextureA3I5), "A3I5: every texel, alpha too");
    check(texture(hlds::TextureFormat::A5I3, 16, 32, 8, kTextureA3I5 | kTextureA5I3), "A5I3: likewise");

    // The PICA's order: every texel somewhere, none twice; the bottom row first.
    {
        std::vector<int> seen(64 * 32, 0);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x)
                ++seen[ctr::picaOffset(x, y, 64, 32)];
        bool once = true;
        for (int s : seen)
            once = once && s == 1;
        check(once, "the PICA's order puts each texel in one place");
        check(ctr::picaOffset(0, 31, 64, 32) == 0 && ctr::picaOffset(1, 31, 64, 32) == 1 &&
                  ctr::picaOffset(0, 30, 64, 32) == 2 && ctr::picaOffset(8, 31, 64, 32) == 64,
              "... in Z order within a tile, from the picture's bottom");
    }


    // A real map, when there is one (HLDS_CTR_NPAC: a cooked .npac): the
    // level put together from its archive and opened, every surface's
    // geometry turned into triangles, every texture of the map decoded.
    if (const char *path = std::getenv("HLDS_CTR_NPAC"))
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<std::uint8_t> npac((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::uint32_t bytes = ctr::npacLevelBytes(npac.data(), npac.size());
        check(bytes != 0, "the map's archive has a level");
        std::vector<std::uint32_t> block((bytes + 3) / 4);
        auto *raw = reinterpret_cast<std::uint8_t *>(block.data());
        check(bytes != 0 && ctr::npacLevel(npac.data(), npac.size(), raw, bytes), "... put together from its pieces");
        LevelView view;
        const LevelError opened = bytes != 0 ? view.open(raw, bytes) : LevelError::TooSmall;
        check(opened == LevelError::Ok, "... and it opens, its checksum right");
        if (opened == LevelError::Ok)
        {
            std::vector<ctr::Vertex> tris(65536);
            long triangles = 0;
            int bad = 0;
            for (std::uint32_t i = 0; i < view.surfaceCount; ++i)
            {
                const Surface &s = view.surfaces[i];
                const int n = ctr::gxTriangles(view.gxStream + s.gxOfs, s.gxWords, tris.data(), static_cast<int>(tris.size()));
                if (n < 0 || n / 3 < s.polygons)
                    ++bad;
                else
                    triangles += n / 3;
            }
            check(bad == 0, "every surface's commands become its triangles (at least its polygons' count)");
            int texBad = 0, texDone = 0;
            for (std::uint32_t i = 0; i < view.textureCount; ++i)
            {
                const TextureRef &ref = view.textures[i];
                char name[48] = "tex/";
                textureFileName(ref.name, ref.width, ref.height, (ref.flags & kTextureA3I5) != 0, name + 4,
                                (ref.flags & kTextureLight) != 0);
                ctr::NpacMember m;
                if (!ctr::npacFind(npac.data(), npac.size(), name, m))
                    continue; // (shared with other maps: in common.npac)
                std::vector<std::uint8_t> file(m.bytes());
                TextureHeader h;
                if (!ctr::npacRead(m, file.data()) || file.size() < sizeof(h))
                {
                    ++texBad;
                    continue;
                }
                std::memcpy(&h, file.data(), sizeof(h));
                std::vector<std::uint32_t> texels(static_cast<std::size_t>(h.width) * h.height);
                if (!textureHeaderValid(h, static_cast<std::uint32_t>(file.size())) ||
                    !ctr::decodeTexture(h, file.data() + sizeof(h), texels.data(), true))
                    ++texBad;
                else
                    ++texDone;
            }
            check(texBad == 0 && texDone > 0, "every texture of the map's own decodes");
            std::printf("  %s: %u surfaces, %ld triangles, %d textures of %u in the map's archive\n", path,
                        view.surfaceCount, triangles, texDone, view.textureCount);
        }
    }

    if (g_failures == 0)
        std::printf("test_ctrdecode: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
