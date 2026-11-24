#pragma once

// Counter-Strike's bot navigation: maps/<map>.nav.
//
// The game's bot (and those after it) walks a map by areas -- rectangles of
// floor, each with the areas it leads to on each side -- worked out from the
// map once, or made by hand. A map that has one is walked far better by the
// port's bots than by the nodes it lays out itself (PathNodes.cpp): its
// areas are gathered into nodes here, and their ways into links.
//
// The file: a magic (0xFEEDFACE), its version, the map's size (version 4 on),
// the names of the map's places (version 5 on), then the areas -- each its
// id, its flags, its corners, its corners' heights, the areas each side,
// its hiding spots, its approaches, its encounter paths, and its place.
// The port reads versions 1 to 9 (Counter-Strike 1.6's and Condition Zero's
// bots write 5).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hlds
{
    inline constexpr std::uint32_t kNavMagic = 0xFEEDFACEu;
    inline constexpr int kNavJump = 0x02;   ///< an area to be jumped to
    inline constexpr int kNavCrouch = 0x01; ///< ... crouched in

    struct NavArea
    {
        std::uint32_t id = 0;
        std::uint32_t flags = 0;
        float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        float neZ = 0, swZ = 0;
        std::vector<std::uint32_t> next; ///< the areas it leads to, every side
        std::uint16_t place = 0;         ///< 0: none; else NavMesh::places[place - 1]
        std::vector<std::array<float, 3>> hiding;
        /// Its middle, on its floor.
        void middle(float out[3]) const
        {
            out[0] = (lo[0] + hi[0]) * 0.5f;
            out[1] = (lo[1] + hi[1]) * 0.5f;
            out[2] = (lo[2] + hi[2] + neZ + swZ) * 0.25f;
        }
    };
    struct NavMesh
    {
        std::uint32_t version = 0;
        std::vector<std::string> places;
        std::vector<NavArea> areas;
    };

    /// False, with why, for anything that is not a whole .nav the port reads.
    bool parseNavMesh(const std::vector<std::uint8_t> &bytes, NavMesh &out, std::string &error);
} // namespace hlds
