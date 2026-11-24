#include "World.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HlCtrNpac.hpp"
#include "world_shbin.h"

namespace ctrport
{
    namespace
    {
        /// A file read whole (malloc'd; null if it cannot be).
        std::uint8_t *readWhole(const char *path, std::size_t &bytes)
        {
            bytes = 0;
            std::FILE *f = std::fopen(path, "rb");
            if (f == nullptr)
                return nullptr;
            std::fseek(f, 0, SEEK_END);
            const long size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            auto *data = size > 0 ? static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(size))) : nullptr;
            if (data != nullptr && std::fread(data, 1, static_cast<std::size_t>(size), f) != static_cast<std::size_t>(size))
            {
                std::free(data);
                data = nullptr;
            }
            std::fclose(f);
            if (data != nullptr)
                bytes = static_cast<std::size_t>(size);
            return data;
        }

        std::uint8_t passOf(std::uint32_t flags)
        {
            if (flags & hldata::kTextureLight)
                return 3;
            if (flags & hldata::kTextureA3I5)
                return 2;
            if (flags & hldata::kTextureColourKey)
                return 1;
            return 0;
        }
    } // namespace

    bool World::load(const char *mapNpac, const char *commonNpac, const char *&error)
    {
        unload();
        std::size_t mapBytes = 0, commonBytes = 0;
        std::uint8_t *map = readWhole(mapNpac, mapBytes);
        if (map == nullptr)
        {
            error = "Map not found on the SD card.";
            return false;
        }
        std::uint8_t *common = readWhole(commonNpac, commonBytes);

        // The level.
        const std::uint32_t bytes = hldata::ctr::npacLevelBytes(map, mapBytes);
        block_ = bytes != 0 ? static_cast<std::uint8_t *>(std::malloc((bytes + 3) & ~3u)) : nullptr;
        if (block_ == nullptr || !hldata::ctr::npacLevel(map, mapBytes, block_, bytes) ||
            view_.open(block_, bytes) != hldata::LevelError::Ok)
        {
            error = "Map file is damaged. Convert again.";
            std::free(map);
            std::free(common);
            unload();
            return false;
        }

        // The geometry: counted, then decoded into the GPU's memory, a batch
        // for each run of one texture.
        static hldata::ctr::Vertex scratch[16384];
        int total = 0;
        for (std::uint32_t i = 0; i < view_.surfaceCount; ++i)
        {
            const hldata::Surface &s = view_.surfaces[i];
            const int n = hldata::ctr::gxTriangles(view_.gxStream + s.gxOfs, s.gxWords, scratch, 16384);
            total += n > 0 ? n : 0;
        }
        vertices_ = static_cast<hldata::ctr::Vertex *>(linearAlloc(sizeof(hldata::ctr::Vertex) * (total + 1)));
        batches_ = static_cast<Batch *>(std::malloc(sizeof(Batch) * (view_.surfaceCount + 1)));
        if (vertices_ == nullptr || batches_ == nullptr)
        {
            error = "Not enough memory for this map.";
            std::free(map);
            std::free(common);
            unload();
            return false;
        }
        for (std::uint32_t i = 0; i < view_.surfaceCount; ++i)
        {
            const hldata::Surface &s = view_.surfaces[i];
            const int n = hldata::ctr::gxTriangles(view_.gxStream + s.gxOfs, s.gxWords, vertices_ + vertexCount_,
                                                   total - vertexCount_);
            if (n <= 0)
                continue;
            const std::uint8_t pass =
                s.texture == hldata::kNoTexture ? kPassOpaque : passOf(view_.textures[s.texture].flags);
            Batch *last = batchCount_ > 0 ? &batches_[batchCount_ - 1] : nullptr;
            if (last != nullptr && last->texture == s.texture && last->first + last->count == static_cast<std::uint32_t>(vertexCount_))
                last->count += static_cast<std::uint32_t>(n);
            else
                batches_[batchCount_++] = Batch{static_cast<std::uint32_t>(vertexCount_), static_cast<std::uint32_t>(n), s.texture, pass};
            vertexCount_ += n;
        }
        GSPGPU_FlushDataCache(vertices_, sizeof(hldata::ctr::Vertex) * vertexCount_);

        const bool texturesOk = loadTextures(map, mapBytes, common, commonBytes);
        std::free(map);
        std::free(common);
        if (!texturesOk)
        {
            error = "Not enough memory for this map's textures.";
            unload();
            return false;
        }

        // The shader.
        shader_ = DVLB_ParseFile(reinterpret_cast<u32 *>(const_cast<u8 *>(world_shbin)), world_shbin_size);
        shaderProgramInit(&program_);
        shaderProgramSetVsh(&program_, &shader_->DVLE[0]);
        uProjection_ = shaderInstanceGetUniformLocation(program_.vertexShader, "projection");
        uModelView_ = shaderInstanceGetUniformLocation(program_.vertexShader, "modelView");
        uTexScale_ = shaderInstanceGetUniformLocation(program_.vertexShader, "texScale");
        return true;
    }

    bool World::loadTextures(const std::uint8_t *map, std::size_t mapBytes, const std::uint8_t *common,
                             std::size_t commonBytes)
    {
        // (What has none: white, so the vertex colour alone is drawn.)
        if (!C3D_TexInit(&white_, 8, 8, GPU_RGBA8))
            return false;
        std::memset(white_.data, 0xFF, 8 * 8 * 4);
        C3D_TexFlush(&white_);

        textureCount_ = static_cast<int>(view_.textureCount);
        textures_ = static_cast<C3D_Tex *>(std::calloc(textureCount_ + 1, sizeof(C3D_Tex)));
        textureLoaded_ = static_cast<bool *>(std::calloc(textureCount_ + 1, sizeof(bool)));
        if (textures_ == nullptr || textureLoaded_ == nullptr)
            return false;
        std::uint8_t *file = nullptr;
        std::size_t fileRoom = 0;
        for (int i = 0; i < textureCount_; ++i)
        {
            const hldata::TextureRef &ref = view_.textures[i];
            char name[48] = "tex/";
            hldata::textureFileName(ref.name, ref.width, ref.height, (ref.flags & hldata::kTextureA3I5) != 0,
                                    name + 4, (ref.flags & hldata::kTextureLight) != 0);
            hldata::ctr::NpacMember m;
            if (!hldata::ctr::npacFind(map, mapBytes, name, m) &&
                (common == nullptr || !hldata::ctr::npacFind(common, commonBytes, name, m)))
                continue; // (drawn in its vertex colour)
            if (m.bytes() > fileRoom)
            {
                std::free(file);
                fileRoom = m.bytes();
                file = static_cast<std::uint8_t *>(std::malloc(fileRoom));
                if (file == nullptr)
                    return false;
            }
            hldata::TextureHeader h;
            if (!hldata::ctr::npacRead(m, file) || m.bytes() < sizeof(h))
                continue;
            std::memcpy(&h, file, sizeof(h));
            if (!hldata::textureHeaderValid(h, m.bytes()))
                continue;
            C3D_Tex &tex = textures_[i];
            if (!C3D_TexInit(&tex, h.width, h.height, GPU_RGBA8))
            {
                std::free(file);
                return false;
            }
            if (!hldata::ctr::decodeTexture(h, file + sizeof(h), static_cast<std::uint32_t *>(tex.data), true))
            {
                C3D_TexDelete(&tex);
                continue;
            }
            C3D_TexFlush(&tex);
            C3D_TexSetFilter(&tex, GPU_LINEAR, GPU_LINEAR);
            C3D_TexSetWrap(&tex, GPU_REPEAT, GPU_REPEAT);
            textureLoaded_[i] = true;
        }
        std::free(file);
        return true;
    }

    void World::unload()
    {
        if (shader_ != nullptr)
        {
            shaderProgramFree(&program_);
            DVLB_Free(shader_);
            shader_ = nullptr;
        }
        for (int i = 0; i < textureCount_; ++i)
        {
            if (textureLoaded_ != nullptr && textureLoaded_[i])
                C3D_TexDelete(&textures_[i]);
        }
        if (white_.data != nullptr)
            C3D_TexDelete(&white_);
        white_ = C3D_Tex{};
        std::free(textures_);
        std::free(textureLoaded_);
        textures_ = nullptr;
        textureLoaded_ = nullptr;
        textureCount_ = 0;
        if (vertices_ != nullptr)
            linearFree(vertices_);
        vertices_ = nullptr;
        vertexCount_ = 0;
        std::free(batches_);
        batches_ = nullptr;
        batchCount_ = 0;
        std::free(block_);
        block_ = nullptr;
        view_ = hldata::LevelView();
    }

    bool World::spawn(int team, float pos[3], float &yaw) const
    {
        // (The counter-terrorists start at info_player_start, the terrorists
        // at info_player_deathmatch: HlCs.hpp.)
        const auto want = team == 1 ? hldata::EntityClass::InfoPlayerDeathmatch : hldata::EntityClass::InfoPlayerStart;
        for (std::uint32_t i = 0; i < view_.entityCount; ++i)
        {
            const hldata::EntityRecord &e = view_.entities[i];
            if (e.classId != static_cast<std::uint16_t>(want))
                continue;
            for (int k = 0; k < 3; ++k)
                pos[k] = static_cast<float>(e.origin[k]) / static_cast<float>(hldata::kFxOne);
            yaw = static_cast<float>(e.angles[1]) * (360.0f / 65536.0f);
            return true;
        }
        return false;
    }

    void World::bindState()
    {
        C3D_BindProgram(&program_);
        C3D_AttrInfo *attr = C3D_GetAttrInfo();
        AttrInfo_Init(attr);
        AttrInfo_AddLoader(attr, 0, GPU_SHORT, 3);         // position
        AttrInfo_AddLoader(attr, 1, GPU_SHORT, 2);         // texture coordinate
        AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4); // colour
        C3D_BufInfo *buf = C3D_GetBufInfo();
        BufInfo_Init(buf);
        BufInfo_Add(buf, vertices_, sizeof(hldata::ctr::Vertex), 3, 0x210);

        // The texture times the vertex colour (the DS's modulate, lit by
        // the converter's light).
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        for (int stage = 1; stage < 6; ++stage)
            C3D_TexEnvInit(C3D_GetTexEnv(stage));
        C3D_CullFace(GPU_CULL_NONE); // (the DS's own test, by the surface's plane, is the next step)
    }

    void World::draw(const C3D_Mtx &projection, const C3D_Mtx &view)
    {
        if (!loaded() || vertexCount_ == 0)
            return;
        bindState();
        C3D_Mtx modelView = view;
        Mtx_Scale(&modelView, 0.25f, 0.25f, 0.25f); // (quarter units)
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uProjection_, &projection);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uModelView_, &modelView);

        for (int pass = kPassOpaque; pass <= kPassLight; ++pass)
        {
            switch (pass)
            {
            case kPassOpaque:
                C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
                C3D_AlphaTest(false, GPU_ALWAYS, 0);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                break;
            case kPassHoles:
                C3D_AlphaTest(true, GPU_GREATER, 0x80);
                break;
            case kPassBlend:
                C3D_AlphaTest(true, GPU_GREATER, 0);
                C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                               GPU_ONE_MINUS_SRC_ALPHA);
                break;
            default:
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
                break;
            }
            for (int b = 0; b < batchCount_; ++b)
            {
                const Batch &batch = batches_[b];
                if (batch.pass != pass)
                    continue;
                const bool has = batch.texture != hldata::kNoTexture && textureLoaded_[batch.texture];
                C3D_Tex *tex = has ? &textures_[batch.texture] : &white_;
                C3D_TexBind(0, tex);
                // 12.4 texels to 0..1, turned up the other way (the PICA's
                // pictures are stored from the bottom; GPU_REPEAT makes -t
                // the same as 1 - t).
                const float su = has ? hldata::ctr::texScale(tex->width) : 0.0f;
                const float sv = has ? -hldata::ctr::texScale(tex->height) : 0.0f;
                C3D_FVUnifSet(GPU_VERTEX_SHADER, uTexScale_, su, sv, 0.0f, 0.0f);
                C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(batch.first), static_cast<int>(batch.count));
            }
        }
        // (Back as citro2d left it: blending on, the alpha test off.)
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
                       GPU_ONE_MINUS_SRC_ALPHA);
    }
} // namespace ctrport
