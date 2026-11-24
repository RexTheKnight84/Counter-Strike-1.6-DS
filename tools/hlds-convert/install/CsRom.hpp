#pragma once

// Counter-Strike 1.6 DS's ROM, made on the player's own computer: the
// console's program (the Half-Life ROM, built into the converter) with the
// player's converted Counter-Strike put in its NitroFS under data/cstrike/,
// where the program looks first (Global::checkRomData), and its own name and
// icon. What tools/cs16_rom.py does with BlocksDS's ndstool, done here, so
// that a player needs nothing but the installer.
//
// The ROM is laid out as ndstool lays one out -- header, ARM9, ARM7, the file
// names, the file table, the banner, the files, then the DSi's ARM9i and
// ARM7i -- and its header's checks made as ndstool makes them: the banner's
// HMAC-SHA1 (at 0x33C), the header's CRC (0x15E) and the header's SHA-1 (at
// 0xFEC, in the place of a signature). A ROM made so holds the player's own
// copy of the game: it is theirs, and not to be shared.

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "RomHeader.hpp"

namespace hlds::install
{
    /// The name the ROM has on the card, at its root.
    inline constexpr const char *kCsRomName = "cs16-ds.nds";

    /// The ROM's banner: what a menu lists it as (three lines).
    inline constexpr const char *kCsRomTitle = "Counter-Strike 1.6 DS\nxRex1984\nBased on CrowbarDS HL1 Port";
    /// ... and the program sent by Download Play, as a console being sent
    /// it lists it.
    inline constexpr const char *kCsChildTitle = "Counter-Strike 1.6 DS\nSent by Download Play\nxRex1984";

    /// A file put in the ROM: its path in NitroFS ("data/cstrike/manifest.bin").
    struct RomFile
    {
        std::string path;
        std::vector<std::uint8_t> bytes;
    };

    /// A banner's icon: 32x32 at 4 bits a pixel in 8x8 tiles, and its 16
    /// colours (BGR555, colour 0 see-through).
    struct BannerIcon
    {
        std::array<std::uint8_t, 512> tiles{};
        std::array<std::uint16_t, 16> palette{};
    };

    /// `rgb`, a 32x32 picture (3 bytes a pixel, rows down), as an icon: its
    /// 15 commonest colours (median cut), colour 0 kept see-through.
    BannerIcon iconFromRgb(std::span<const std::uint8_t, 32 * 32 * 3> rgb);

    /// A picture of any size (`rgba`, 4 bytes a pixel, rows down) as an
    /// icon: made 32x32 (each pixel the average of what it covers), on
    /// black where it is see-through.
    BannerIcon iconFromPicture(std::span<const std::uint8_t> rgba, int width, int height);

    /// What the game reads of a converted Counter-Strike (the converter's
    /// cstrike data folder: <card>/_nds/hl_nds/data/cstrike): its archives,
    /// its manifest, maps/, music/ and air/ (what is sent by Download Play)
    /// -- not what the game writes there -- as the ROM's data/cstrike/ files.
    /// False, with `why`, if it has no manifest or archive.
    bool gatherCsGame(const std::filesystem::path &data, std::vector<RomFile> &out, std::string &why);

    struct CsRomLook
    {
        std::string title = kCsRomTitle;
        std::string childTitle = kCsChildTitle;
        bool hasIcon = false; ///< false: the program's own icon
        BannerIcon icon;
    };

    /// `base` (the console's program: its game code WHLD or ####) with
    /// `files` added to its NitroFS, named and pictured as `look` has it,
    /// made for `device` (the 3DS's game code WCSD -- TWiLight Menu++ offers
    /// it the wide screen -- the others' ####). False, with `why`, for a
    /// `base` that is not the program, or a file already there.
    bool buildCsRom(std::span<const std::uint8_t> base, const std::vector<RomFile> &files, const CsRomLook &look,
                    Device device, std::vector<std::uint8_t> &out, std::string &why);

    /// The paths of a ROM's NitroFS files, and where each is (offset, size):
    /// for checking one made.
    struct RomFileAt
    {
        std::string path;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };
    bool romFiles(std::span<const std::uint8_t> rom, std::vector<RomFileAt> &out, std::string &why);

    /// SHA-1, and HMAC-SHA1 (for the header's checks; exposed to be tested).
    std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> bytes);
    std::array<std::uint8_t, 20> hmacSha1(std::span<const std::uint8_t> key, std::span<const std::uint8_t> bytes);
} // namespace hlds::install
