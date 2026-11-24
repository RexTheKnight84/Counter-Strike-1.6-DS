#pragma once

// The world, drawn by the 3DS's GPU: a map's level and textures, as the DS
// ROM has them on the card (HlCtrNpac.hpp, HlCtrDecode.hpp), uploaded once
// when the map is loaded and drawn whole each frame.
//
// Whole, for now: a Counter-Strike map is 10 to 22 thousand triangles, which
// the PICA draws at 30 frames a second without the DS's walk of the tree and
// its budget. The leaves' PVS is the next step, for the bigger maps.
//
// The surfaces are stored sorted by texture: one draw for each run of a
// texture. What is see-through is drawn after what is not, without writing
// depth: holes (a grate's colour key) by the alpha test, A3I5 by blending,
// and what is cooked as light added to what is behind it.

#include <3ds.h>
#include <citro3d.h>

#include "HlCtrDecode.hpp"
#include "HlLevelReader.hpp"

namespace ctrport
{
    class World
    {
    public:
        /// `mapNpac`: the map's archive; `commonNpac`: the textures the maps
        /// share. False, with `error` said, if it cannot be drawn.
        bool load(const char *mapNpac, const char *commonNpac, const char *&error);
        void unload();
        bool loaded() const { return view_.header != nullptr; }

        /// Where a player of `team` (HlCs.hpp's kTeam*: 1 terrorist, 2
        /// counter-terrorist) starts: the first of the map's starts for
        /// that side. Units; yaw in degrees.
        bool spawn(int team, float pos[3], float &yaw) const;

        /// Draw it: `projection`, and `view` in units (the level's quarter
        /// units are scaled here).
        void draw(const C3D_Mtx &projection, const C3D_Mtx &view);

        int triangles() const { return vertexCount_ / 3; }
        int batches() const { return batchCount_; }
        int textures() const { return textureCount_; }

    private:
        struct Batch
        {
            std::uint32_t first, count; ///< vertices
            std::uint16_t texture;      ///< into textures_, or kNoTexture
            std::uint8_t pass;          ///< kPass*
        };
        static constexpr std::uint8_t kPassOpaque = 0, kPassHoles = 1, kPassBlend = 2, kPassLight = 3;

        bool loadTextures(const std::uint8_t *map, std::size_t mapBytes, const std::uint8_t *common,
                          std::size_t commonBytes);
        void bindState();

        std::uint8_t *block_ = nullptr; ///< the level
        hldata::LevelView view_;
        hldata::ctr::Vertex *vertices_ = nullptr; ///< in linear memory, for the GPU
        int vertexCount_ = 0;
        Batch *batches_ = nullptr;
        int batchCount_ = 0;
        C3D_Tex *textures_ = nullptr;
        bool *textureLoaded_ = nullptr;
        int textureCount_ = 0;
        C3D_Tex white_{};

        DVLB_s *shader_ = nullptr;
        shaderProgram_s program_{};
        int uProjection_ = -1, uModelView_ = -1, uTexScale_ = -1;
    };
} // namespace ctrport
