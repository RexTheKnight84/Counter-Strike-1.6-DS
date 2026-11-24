#include "CsRom.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

namespace hlds::install
{
    namespace
    {
        std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t at)
        {
            return static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16) |
                   static_cast<std::uint32_t>(d[at + 3]) << 24;
        }
        std::uint16_t u16(std::span<const std::uint8_t> d, std::size_t at)
        {
            return static_cast<std::uint16_t>(d[at] | d[at + 1] << 8);
        }
        void put32(std::vector<std::uint8_t> &d, std::size_t at, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                d[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
        }
        void put16(std::vector<std::uint8_t> &d, std::size_t at, std::uint16_t v)
        {
            d[at] = static_cast<std::uint8_t>(v);
            d[at + 1] = static_cast<std::uint8_t>(v >> 8);
        }
        std::uint32_t align(std::uint32_t v, std::uint32_t to) { return (v + to - 1) / to * to; }

        // Header fields.
        constexpr std::size_t kUnitCodeAt = 0x12, kCapacityAt = 0x14, kArm7At = 0x30, kArm7SizeAt = 0x3C;
        constexpr std::size_t kFntAt = 0x40, kFntSizeAt = 0x44, kFatAt = 0x48, kFatSizeAt = 0x4C, kBannerAt = 0x68;
        constexpr std::size_t kNtrSizeAt = 0x80, kArm9iAt = 0x1C0, kArm9iSizeAt = 0x1CC, kArm7iAt = 0x1D0,
                              kArm7iSizeAt = 0x1DC, kTwlSizeAt = 0x210, kTitleIdAt = 0x230, kBannerHmacAt = 0x33C,
                              kHeaderShaAt = 0xFEC, kHeaderShaOf = 0xE00;
        constexpr std::uint32_t kBannerBytes = 0x840; // (version 1: six languages)
        // The DSi's HMAC key ndstool signs a homebrew's banner with.
        constexpr std::uint8_t kHmacKey[64] = {
            0x21, 0x06, 0xC0, 0xDE, 0xBA, 0x98, 0xCE, 0x3F, 0xA6, 0x92, 0xE3, 0x9D, 0x46, 0xF2, 0xED, 0x01,
            0x76, 0xE3, 0xCC, 0x08, 0x56, 0x23, 0x63, 0xFA, 0xCA, 0xD4, 0xEC, 0xDF, 0x9A, 0x62, 0x78, 0x34,
            0x8F, 0x6D, 0x63, 0x3C, 0xFE, 0x22, 0xCA, 0x92, 0x20, 0x88, 0x97, 0x23, 0xD2, 0xCF, 0xAE, 0xC2,
            0x32, 0x67, 0x8D, 0xFE, 0xCA, 0x83, 0x64, 0x98, 0xAC, 0xFD, 0x3E, 0x37, 0x87, 0x46, 0x58, 0x24};

        std::uint16_t crc16(std::span<const std::uint8_t> bytes) { return headerCrc(bytes); }

        /// A banner's titles (every language its version has) and icon, and
        /// its CRCs made again.
        void nameBanner(std::vector<std::uint8_t> &rom, std::size_t at, const std::string &title, const BannerIcon *icon)
        {
            if (at == 0 || at + kBannerBytes > rom.size())
                return;
            const std::uint16_t version = u16(rom, at);
            if (icon != nullptr)
            {
                std::memcpy(rom.data() + at + 0x20, icon->tiles.data(), 512);
                for (int i = 0; i < 16; ++i)
                    put16(rom, at + 0x220 + i * 2, icon->palette[i]);
            }
            std::vector<std::uint16_t> text;
            for (const char c : title)
                text.push_back(static_cast<std::uint8_t>(c));
            if (text.size() > 127)
                text.resize(127);
            const int languages = 6 + (version >= 2 ? 1 : 0) + (version >= 3 ? 1 : 0);
            for (int lang = 0; lang < languages; ++lang)
            {
                const std::size_t slot = at + 0x240 + static_cast<std::size_t>(lang) * 0x100;
                if (slot + 0x100 > rom.size())
                    break;
                std::fill(rom.begin() + static_cast<std::ptrdiff_t>(slot), rom.begin() + static_cast<std::ptrdiff_t>(slot + 0x100), 0);
                for (std::size_t i = 0; i < text.size(); ++i)
                    put16(rom, slot + i * 2, text[i]);
            }
            const std::span<const std::uint8_t> all(rom);
            put16(rom, at + 2, crc16(all.subspan(at + 0x20, 0x820)));
            if (version >= 2 && at + 0x940 <= rom.size())
                put16(rom, at + 4, crc16(all.subspan(at + 0x20, 0x920)));
            if (version >= 3 && at + 0xA40 <= rom.size())
                put16(rom, at + 6, crc16(all.subspan(at + 0x20, 0xA20)));
        }

        struct Dir
        {
            std::map<std::string, std::unique_ptr<Dir>> dirs;
            std::map<std::string, std::size_t> files; ///< name -> index into the list of all
            int id = 0;
            int parent = 0;
            int firstFile = 0;
        };
    } // namespace

    // ------------------------------------------------------------------ SHA-1

    std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> bytes)
    {
        std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        std::vector<std::uint8_t> m(bytes.begin(), bytes.end());
        const std::uint64_t bits = static_cast<std::uint64_t>(bytes.size()) * 8;
        m.push_back(0x80);
        while (m.size() % 64 != 56)
            m.push_back(0);
        for (int i = 7; i >= 0; --i)
            m.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
        auto rol = [](std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
        for (std::size_t block = 0; block < m.size(); block += 64)
        {
            std::uint32_t w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = static_cast<std::uint32_t>(m[block + i * 4]) << 24 | static_cast<std::uint32_t>(m[block + i * 4 + 1]) << 16 |
                       static_cast<std::uint32_t>(m[block + i * 4 + 2]) << 8 | m[block + i * 4 + 3];
            for (int i = 16; i < 80; ++i)
                w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i)
            {
                std::uint32_t f, k;
                if (i < 20)
                    f = (b & c) | (~b & d), k = 0x5A827999;
                else if (i < 40)
                    f = b ^ c ^ d, k = 0x6ED9EBA1;
                else if (i < 60)
                    f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
                else
                    f = b ^ c ^ d, k = 0xCA62C1D6;
                const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = rol(b, 30);
                b = a;
                a = t;
            }
            h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
        }
        std::array<std::uint8_t, 20> out{};
        for (int i = 0; i < 20; ++i)
            out[i] = static_cast<std::uint8_t>(h[i / 4] >> (24 - 8 * (i % 4)));
        return out;
    }

    std::array<std::uint8_t, 20> hmacSha1(std::span<const std::uint8_t> key, std::span<const std::uint8_t> bytes)
    {
        std::uint8_t k[64] = {};
        if (key.size() > 64)
        {
            const auto hashed = sha1(key);
            std::memcpy(k, hashed.data(), hashed.size());
        }
        else
            std::memcpy(k, key.data(), key.size());
        std::vector<std::uint8_t> inner(64 + bytes.size());
        for (int i = 0; i < 64; ++i)
            inner[i] = k[i] ^ 0x36;
        std::copy(bytes.begin(), bytes.end(), inner.begin() + 64);
        const auto first = sha1(inner);
        std::vector<std::uint8_t> outer(64 + 20);
        for (int i = 0; i < 64; ++i)
            outer[i] = k[i] ^ 0x5C;
        std::copy(first.begin(), first.end(), outer.begin() + 64);
        return sha1(outer);
    }

    // ------------------------------------------------------------------- icon

    BannerIcon iconFromRgb(std::span<const std::uint8_t, 32 * 32 * 3> rgb)
    {
        // Median cut: the box of colours with the widest spread split at its
        // middle, until there are 15.
        struct Box
        {
            std::vector<int> pixels;
        };
        std::vector<Box> boxes(1);
        for (int i = 0; i < 32 * 32; ++i)
            boxes[0].pixels.push_back(i);
        auto spread = [&](const Box &box, int &channel)
        {
            int best = -1;
            for (int c = 0; c < 3; ++c)
            {
                int lo = 255, hi = 0;
                for (const int p : box.pixels)
                {
                    lo = std::min<int>(lo, rgb[p * 3 + c]);
                    hi = std::max<int>(hi, rgb[p * 3 + c]);
                }
                if (hi - lo > best)
                    best = hi - lo, channel = c;
            }
            return best;
        };
        while (boxes.size() < 15)
        {
            int which = -1, widest = 0, channel = 0;
            for (std::size_t b = 0; b < boxes.size(); ++b)
            {
                int c = 0;
                const int s = boxes[b].pixels.size() > 1 ? spread(boxes[b], c) : 0;
                if (s > widest)
                    widest = s, which = static_cast<int>(b), channel = c;
            }
            if (which < 0)
                break;
            Box &box = boxes[static_cast<std::size_t>(which)];
            std::sort(box.pixels.begin(), box.pixels.end(),
                      [&](int a, int b) { return rgb[a * 3 + channel] < rgb[b * 3 + channel]; });
            Box half;
            half.pixels.assign(box.pixels.begin() + static_cast<std::ptrdiff_t>(box.pixels.size() / 2), box.pixels.end());
            box.pixels.resize(box.pixels.size() / 2);
            boxes.push_back(std::move(half));
        }
        BannerIcon icon;
        std::uint8_t index[32 * 32] = {};
        for (std::size_t b = 0; b < boxes.size(); ++b)
        {
            int sum[3] = {0, 0, 0};
            for (const int p : boxes[b].pixels)
                for (int c = 0; c < 3; ++c)
                    sum[c] += rgb[p * 3 + c];
            const int n = std::max<int>(1, static_cast<int>(boxes[b].pixels.size()));
            const int r = sum[0] / n, g = sum[1] / n, bl = sum[2] / n;
            icon.palette[b + 1] = static_cast<std::uint16_t>((r >> 3) | (g >> 3) << 5 | (bl >> 3) << 10);
            for (const int p : boxes[b].pixels)
                index[p] = static_cast<std::uint8_t>(b + 1);
        }
        std::size_t at = 0;
        for (int ty = 0; ty < 4; ++ty)
            for (int tx = 0; tx < 4; ++tx)
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; x += 2)
                    {
                        const int p = (ty * 8 + y) * 32 + tx * 8 + x;
                        icon.tiles[at++] = static_cast<std::uint8_t>(index[p] | index[p + 1] << 4);
                    }
        return icon;
    }

    BannerIcon iconFromPicture(std::span<const std::uint8_t> rgba, int width, int height)
    {
        std::array<std::uint8_t, 32 * 32 * 3> small{};
        if (width > 0 && height > 0 && rgba.size() >= static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        {
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                {
                    const int x0 = x * width / 32, x1 = std::max(x0 + 1, (x + 1) * width / 32);
                    const int y0 = y * height / 32, y1 = std::max(y0 + 1, (y + 1) * height / 32);
                    long sum[3] = {0, 0, 0};
                    long n = 0;
                    for (int yy = y0; yy < y1 && yy < height; ++yy)
                        for (int xx = x0; xx < x1 && xx < width; ++xx)
                        {
                            const std::uint8_t *p = &rgba[(static_cast<std::size_t>(yy) * width + xx) * 4];
                            for (int c = 0; c < 3; ++c)
                                sum[c] += p[c] * p[3] / 255; // (on black)
                            ++n;
                        }
                    for (int c = 0; c < 3; ++c)
                        small[(y * 32 + x) * 3 + c] = static_cast<std::uint8_t>(n ? sum[c] / n : 0);
                }
        }
        return iconFromRgb(small);
    }

    bool gatherCsGame(const std::filesystem::path &data, std::vector<RomFile> &out, std::string &why)
    {
        namespace fs = std::filesystem;
        out.clear();
        auto take = [&](const fs::path &file, const std::string &name) -> bool
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                why = "cannot read " + file.string();
                return false;
            }
            RomFile f;
            f.path = "data/cstrike/" + name;
            f.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            out.push_back(std::move(f));
            return true;
        };
        for (const char *name : {"common.npac", "manifest.bin"})
        {
            if (!fs::is_regular_file(data / name))
            {
                why = "no " + std::string(name) + " in " + data.string() + ": is Counter-Strike converted there?";
                return false;
            }
            if (!take(data / name, name))
                return false;
        }
        std::error_code ec;
        for (const char *folder : {"maps", "music", "air"})
        {
            if (!fs::is_directory(data / folder))
                continue;
            std::vector<fs::path> found;
            for (fs::recursive_directory_iterator it(data / folder, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file())
                    found.push_back(it->path());
            std::sort(found.begin(), found.end());
            for (const fs::path &file : found)
            {
                const std::string name = fs::relative(file, data).generic_string();
                // (Not a cook cut short's temporary, nor anything not the game's.)
                if (file.extension().string().rfind(".tmp", 0) == 0)
                    continue;
                if (!take(file, name))
                    return false;
            }
        }
        return true;
    }

    // --------------------------------------------------------------- NitroFS

    bool romFiles(std::span<const std::uint8_t> rom, std::vector<RomFileAt> &out, std::string &why)
    {
        out.clear();
        if (rom.size() < 0x200)
        {
            why = "too short to be a ROM";
            return false;
        }
        const std::uint32_t fnt = u32(rom, kFntAt), fntSize = u32(rom, kFntSizeAt), fat = u32(rom, kFatAt),
                            fatSize = u32(rom, kFatSizeAt);
        if (fnt == 0 || fnt + fntSize > rom.size() || fat + fatSize > rom.size())
        {
            why = "its file system is not whole";
            return false;
        }
        bool ok = true;
        // (Depth-first, as the file names are kept: each folder's table.)
        auto walk = [&](auto &self, int dir, const std::string &path, int depth) -> void
        {
            if (!ok || depth > 32)
            {
                ok = false;
                return;
            }
            const std::size_t entry = fnt + static_cast<std::size_t>(dir & 0xFFF) * 8;
            if (entry + 8 > fnt + fntSize)
            {
                ok = false;
                return;
            }
            std::size_t p = fnt + u32(rom, entry);
            int file = u16(rom, entry + 4);
            while (ok && p < fnt + fntSize)
            {
                const std::uint8_t length = rom[p++];
                if (length == 0)
                    return;
                const std::size_t nameLength = length & 0x7F;
                if (p + nameLength > fnt + fntSize)
                {
                    ok = false;
                    return;
                }
                const std::string name(reinterpret_cast<const char *>(rom.data() + p), nameLength);
                p += nameLength;
                if (length & 0x80)
                {
                    const int child = u16(rom, p);
                    p += 2;
                    self(self, child, path + name + "/", depth + 1);
                }
                else
                {
                    const std::size_t slot = fat + static_cast<std::size_t>(file) * 8;
                    if (slot + 8 > fat + fatSize)
                    {
                        ok = false;
                        return;
                    }
                    const std::uint32_t start = u32(rom, slot), end = u32(rom, slot + 4);
                    if (end < start || end > rom.size())
                    {
                        ok = false;
                        return;
                    }
                    out.push_back({path + name, start, end - start});
                    ++file;
                }
            }
        };
        walk(walk, 0xF000, "", 0);
        if (!ok)
            why = "its file system is not whole";
        return ok;
    }

    bool buildCsRom(std::span<const std::uint8_t> base, const std::vector<RomFile> &files, const CsRomLook &look,
                    Device device, std::vector<std::uint8_t> &out, std::string &why)
    {
        // The program itself, checked as the Half-Life ROM put on a card is.
        std::vector<std::uint8_t> program;
        if (!romFor(base, Device::Ds, program, why))
            return false;
        const std::span<const std::uint8_t> rom(program);
        std::vector<RomFileAt> had;
        if (!romFiles(rom, had, why))
            return false;
        const bool twl = (rom[kUnitCodeAt] & 2) != 0;
        const std::uint32_t arm7End = u32(rom, kArm7At) + u32(rom, kArm7SizeAt);
        const std::uint32_t banner = u32(rom, kBannerAt);
        if (arm7End > u32(rom, kFntAt) || banner == 0 || banner + kBannerBytes > rom.size())
        {
            why = "not laid out as the program is";
            return false;
        }

        // Every file: the program's own, then the game's.
        std::vector<RomFile> all;
        for (const RomFileAt &f : had)
        {
            RomFile copy;
            copy.path = f.path;
            copy.bytes.assign(rom.begin() + f.offset, rom.begin() + f.offset + f.size);
            if (f.path == "dlplay/child.nds" && copy.bytes.size() > 0x200)
                nameBanner(copy.bytes, u32(copy.bytes, kBannerAt), look.childTitle, look.hasIcon ? &look.icon : nullptr);
            all.push_back(std::move(copy));
        }
        for (const RomFile &f : files)
        {
            for (const RomFile &there : all)
            {
                if (there.path == f.path)
                {
                    why = "two files called " + f.path;
                    return false;
                }
            }
            if (f.path.empty() || f.path.front() == '/' || f.path.back() == '/')
            {
                why = "no file can be called \"" + f.path + "\"";
                return false;
            }
            all.push_back(f);
        }

        // The folders, as a tree; each folder's files numbered together.
        Dir root;
        std::vector<Dir *> dirs;
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            Dir *at = &root;
            std::string rest = all[i].path;
            for (std::size_t slash; (slash = rest.find('/')) != std::string::npos; rest = rest.substr(slash + 1))
            {
                std::unique_ptr<Dir> &next = at->dirs[rest.substr(0, slash)];
                if (!next)
                    next = std::make_unique<Dir>();
                at = next.get();
            }
            if (rest.size() > 127)
            {
                why = "a name too long: " + rest;
                return false;
            }
            at->files[rest] = i;
        }
        std::vector<std::size_t> order; // (file id -> index in `all`)
        auto number = [&](auto &self, Dir &dir, int parent) -> void
        {
            dir.id = static_cast<int>(dirs.size());
            dir.parent = parent;
            dirs.push_back(&dir);
            dir.firstFile = static_cast<int>(order.size());
            for (const auto &[name, index] : dir.files)
                order.push_back(index);
            for (auto &[name, child] : dir.dirs)
                self(self, *child, dir.id);
        };
        number(number, root, 0);
        if (dirs.size() > 0xFFF || order.size() > 0xF000)
        {
            why = "too many files";
            return false;
        }

        // The file names: a table of the folders, then each one's names.
        std::vector<std::uint8_t> fnt(dirs.size() * 8);
        for (Dir *dir : dirs)
        {
            const std::size_t entry = static_cast<std::size_t>(dir->id) * 8;
            put32(fnt, entry, static_cast<std::uint32_t>(fnt.size()));
            put16(fnt, entry + 4, static_cast<std::uint16_t>(dir->firstFile));
            put16(fnt, entry + 6, static_cast<std::uint16_t>(dir->id == 0 ? dirs.size() : 0xF000 + dir->parent));
            for (const auto &[name, index] : dir->files)
            {
                fnt.push_back(static_cast<std::uint8_t>(name.size()));
                fnt.insert(fnt.end(), name.begin(), name.end());
            }
            for (const auto &[name, child] : dir->dirs)
            {
                if (name.size() > 127)
                {
                    why = "a name too long: " + name;
                    return false;
                }
                fnt.push_back(static_cast<std::uint8_t>(0x80 | name.size()));
                fnt.insert(fnt.end(), name.begin(), name.end());
                const std::uint16_t id = static_cast<std::uint16_t>(0xF000 + child->id);
                fnt.push_back(static_cast<std::uint8_t>(id));
                fnt.push_back(static_cast<std::uint8_t>(id >> 8));
            }
            fnt.push_back(0);
        }

        // Where everything goes, as ndstool puts it.
        const std::uint32_t fntAt = u32(rom, kFntAt);
        const std::uint32_t fatAt = align(fntAt + static_cast<std::uint32_t>(fnt.size()), 0x200);
        const std::uint32_t fatSize = static_cast<std::uint32_t>(order.size() * 8);
        // (After the file table, ndstool's mark of a ROM with its files in
        // it, "NitroFS!": BlocksDS's NitroFS looks for it.)
        const std::uint32_t bannerAt = align(fatAt + fatSize + 8, 0x200);
        std::uint32_t at = align(bannerAt + kBannerBytes, 0x200);
        std::vector<std::uint32_t> starts(order.size());
        for (std::size_t id = 0; id < order.size(); ++id)
        {
            starts[id] = at;
            const std::uint64_t end = static_cast<std::uint64_t>(at) + all[order[id]].bytes.size();
            if (end > 0xF0000000ull)
            {
                why = "too big for a ROM";
                return false;
            }
            at = align(static_cast<std::uint32_t>(end), 0x200);
        }
        std::uint32_t ntrEnd = order.empty() ? bannerAt + kBannerBytes
                                             : starts.back() + static_cast<std::uint32_t>(all[order.back()].bytes.size());
        std::uint32_t total = align(ntrEnd, 0x200);
        std::uint32_t arm9iAt = 0, arm7iAt = 0;
        const std::uint32_t arm9iSize = twl ? u32(rom, kArm9iSizeAt) : 0, arm7iSize = twl ? u32(rom, kArm7iSizeAt) : 0;
        if (twl)
        {
            if (u32(rom, kArm9iAt) + arm9iSize > rom.size() || u32(rom, kArm7iAt) + arm7iSize > rom.size())
            {
                why = "its DSi part is not whole";
                return false;
            }
            arm9iAt = align(ntrEnd, 0x200);
            arm7iAt = align(arm9iAt + arm9iSize, 0x200);
            total = align(arm7iAt + arm7iSize, 0x200);
        }

        out.assign(total, 0);
        // Header, ARM9, ARM7: as they are.
        std::copy(rom.begin(), rom.begin() + fntAt, out.begin());
        std::copy(fnt.begin(), fnt.end(), out.begin() + fntAt);
        for (std::size_t id = 0; id < order.size(); ++id)
        {
            put32(out, fatAt + id * 8, starts[id]);
            put32(out, fatAt + id * 8 + 4, starts[id] + static_cast<std::uint32_t>(all[order[id]].bytes.size()));
            const std::vector<std::uint8_t> &bytes = all[order[id]].bytes;
            std::copy(bytes.begin(), bytes.end(), out.begin() + starts[id]);
        }
        std::memcpy(out.data() + fatAt + fatSize, "NitroFS!", 8);
        std::copy(rom.begin() + banner, rom.begin() + banner + kBannerBytes, out.begin() + bannerAt);
        nameBanner(out, bannerAt, look.title, look.hasIcon ? &look.icon : nullptr);
        if (twl)
        {
            const std::uint32_t was9 = u32(rom, kArm9iAt), was7 = u32(rom, kArm7iAt);
            std::copy(rom.begin() + was9, rom.begin() + was9 + arm9iSize, out.begin() + arm9iAt);
            std::copy(rom.begin() + was7, rom.begin() + was7 + arm7iSize, out.begin() + arm7iAt);
        }

        // The header told where things are, and its checks made again.
        static constexpr char kWideCs[4] = {'W', 'C', 'S', 'D'};
        static constexpr char kHomebrew[4] = {'#', '#', '#', '#'};
        std::memcpy(out.data() + kGameCodeAt, device == Device::ThreeDs ? kWideCs : kHomebrew, 4);
        int capacity = 0;
        while ((0x20000ull << capacity) < total && capacity < 15)
            ++capacity;
        out[kCapacityAt] = static_cast<std::uint8_t>(capacity);
        put32(out, kFntAt, fntAt);
        put32(out, kFntSizeAt, static_cast<std::uint32_t>(fnt.size()));
        put32(out, kFatAt, fatAt);
        put32(out, kFatSizeAt, fatSize);
        put32(out, kBannerAt, bannerAt);
        put32(out, kNtrSizeAt, ntrEnd);
        if (twl)
        {
            put32(out, kArm9iAt, arm9iAt);
            put32(out, kArm7iAt, arm7iAt);
            put32(out, kTwlSizeAt, total);
            for (int i = 0; i < 4; ++i)
                out[kTitleIdAt + i] = out[kGameCodeAt + 3 - i];
            const auto mac = hmacSha1(kHmacKey, std::span<const std::uint8_t>(out).subspan(bannerAt, kBannerBytes));
            std::copy(mac.begin(), mac.end(), out.begin() + kBannerHmacAt);
        }
        const std::uint16_t crc = headerCrc(std::span<const std::uint8_t>(out).first(kHeaderCrcAt));
        put16(out, kHeaderCrcAt, crc);
        if (twl)
        {
            const auto sum = sha1(std::span<const std::uint8_t>(out).first(kHeaderShaOf));
            std::copy(sum.begin(), sum.end(), out.begin() + kHeaderShaAt);
        }
        return true;
    }
} // namespace hlds::install
