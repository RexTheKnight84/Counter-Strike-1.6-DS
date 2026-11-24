#pragma once

// A map's archive (.npac: HlNpacIndex.hpp), read whole into memory, as the 3DS
// has the room to (ctr/): a member found by its path, and its bytes undone if
// they are packed (the DS BIOS's LZ77: HlLz77Stream.hpp); and a level put
// together from its pieces (level.0, level.1...: HlLevel.hpp), as the DS's
// Level::load puts it together from the card.
//
// std-only, no heap of its own (the caller gives the room): the host tests
// run it on a real map (tests/test_ctrdecode.cpp, HLDS_CTR_NPAC).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HlLevel.hpp"
#include "HlLz77Stream.hpp"
#include "HlNpacIndex.hpp"

namespace hldata::ctr
{
    struct NpacMember
    {
        const std::uint8_t *stored = nullptr; ///< its bytes in the archive
        std::uint32_t storedBytes = 0;
        std::uint32_t packing = 0; ///< the BIOS header word: low byte 0x10, LZ77; 0, as it is
        /// What it comes to: its packing word's top 24 bits, or what is stored.
        std::uint32_t bytes() const { return (packing & 0xFF) == 0x10 ? packing >> 8 : storedBytes; }
        bool packed() const { return (packing & 0xFF) == 0x10; }
    };

    /// Member `path` of the archive at `data` (`size` bytes, the whole file).
    inline bool npacFind(const std::uint8_t *data, std::size_t size, const char *path, NpacMember &out)
    {
        const std::uint32_t want = npacPathHash(path, std::strlen(path));
        bool found = false;
        std::uint32_t files = 0;
        const bool ok = npacMembers(data, size, files,
                                    [&](std::uint32_t, std::uint32_t hash, std::uint32_t at, std::uint32_t stored,
                                        std::uint32_t packing) {
                                        if (found || hash != want || at + stored > size)
                                            return;
                                        out.stored = data + at;
                                        out.storedBytes = stored;
                                        out.packing = packing;
                                        found = true;
                                    });
        return ok && found;
    }

    /// The member's bytes, undone, into `out` (room: member.bytes()).
    inline bool npacRead(const NpacMember &m, std::uint8_t *out)
    {
        if (!m.packed())
        {
            std::memcpy(out, m.stored, m.storedBytes);
            return true;
        }
        Lz77Stream lz;
        lz.begin(m.bytes());
        const std::uint8_t *in = m.stored;
        std::size_t left = m.storedBytes;
        const std::size_t wrote = lz.pump(in, left, out, m.bytes());
        return wrote == m.bytes() && lz.done();
    }

    /// The level's size, from level.0's header (0: no level, or a damaged one).
    inline std::uint32_t npacLevelBytes(const std::uint8_t *data, std::size_t size)
    {
        NpacMember m;
        if (!npacFind(data, size, "level.0", m) || m.bytes() != kLevelDataStart)
            return 0;
        alignas(4) std::uint8_t first[kLevelDataStart];
        if (!npacRead(m, first))
            return 0;
        LevelHeader header;
        std::memcpy(&header, first, sizeof(header));
        return header.fileSize >= kLevelDataStart && header.fileSize <= 8u * 1024 * 1024 ? header.fileSize : 0;
    }

    /// The level, whole, into `block` (npacLevelBytes() of room, 4-aligned):
    /// each piece to its place.
    inline bool npacLevel(const std::uint8_t *data, std::size_t size, std::uint8_t *block, std::uint32_t bytes)
    {
        for (std::uint32_t piece = 0; piece < levelPieces(bytes); ++piece)
        {
            char name[16] = "level.";
            int n = 6;
            char digits[10];
            int d = 0;
            std::uint32_t v = piece;
            do
                digits[d++] = static_cast<char>('0' + v % 10);
            while ((v /= 10) != 0);
            while (d > 0)
                name[n++] = digits[--d];
            name[n] = '\0';
            NpacMember m;
            if (!npacFind(data, size, name, m) || m.bytes() != levelPieceBytes(bytes, piece) ||
                !npacRead(m, block + levelPieceStart(piece)))
                return false;
        }
        return true;
    }
} // namespace hldata::ctr
