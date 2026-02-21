#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Engine/Renderer/IndirectBuffer.h"

namespace Engine { class Shader; }

namespace Engine {

    class WorldRenderer {
    public:
        WorldRenderer();
        ~WorldRenderer();

        WorldRenderer(const WorldRenderer&) = delete;
        WorldRenderer& operator=(const WorldRenderer&) = delete;

        void UploadOrUpdateChunk(const glm::ivec3& chunkCoord,
                                 const std::vector<uint32_t>& vertices,
                                 const std::vector<unsigned int>& indices);

        void RemoveChunk(const glm::ivec3& chunkCoord);

        void PrepareVisibleDraws(const glm::mat4& viewProjection,
                     const glm::vec3& cameraPos,
                     float cullDistance);

        void DrawAll(Shader& shader) const;

        std::size_t GetChunkCount() const { return m_Chunks.size(); }

        // Stats (updated when the super-buffer is rebuilt).
        std::size_t GetLastDrawCount() const { return m_LastDrawCount; }
        std::size_t GetLastIndexCount() const { return m_LastIndexCount; }
        std::size_t GetLastTriangleCount() const { return m_LastIndexCount / 3; }

    private:
        struct ChunkKey {
            glm::ivec3 coord{0};
            bool operator==(const ChunkKey& other) const { return coord == other.coord; }
        };

        struct ChunkKeyHash {
            std::size_t operator()(const ChunkKey& k) const noexcept {
                const uint32_t x = (uint32_t)k.coord.x;
                const uint32_t y = (uint32_t)k.coord.y;
                const uint32_t z = (uint32_t)k.coord.z;
                uint32_t h = 2166136261u;
                auto mix = [&](uint32_t v) {
                    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
                };
                mix(x);
                mix(y);
                mix(z);
                return (std::size_t)h;
            }
        };

        struct ChunkDraw {
            glm::ivec3 coord{0};
            std::vector<uint32_t> vertices;
            std::vector<unsigned int> indices;

            // GPU allocation + draw table slot
            uint32_t vertexOffset = 0;
            uint32_t vertexCapacity = 0;
            uint32_t vertexCount = 0;

            uint32_t indexOffset = 0;
            uint32_t indexCapacity = 0;
            uint32_t indexCount = 0;

            uint32_t drawIndex = 0;
            bool hasGPU = false;
        };

        struct FreeBlock {
            uint32_t offset = 0;
            uint32_t size = 0;
        };

        uint32_t Alloc(std::vector<FreeBlock>& freeList, uint32_t size);
        void Free(std::vector<FreeBlock>& freeList, uint32_t offset, uint32_t size);
        void MergeFreeList(std::vector<FreeBlock>& freeList);

        void EnsureCapacity(uint32_t neededVerts, uint32_t neededIndices, uint32_t neededDraws);
        void ReuploadAllGPU();
        void UploadOrResizeChunkGPU(ChunkDraw& chunk);
        void UpdateStats() const;

        // Per-chunk CPU meshes; used to rebuild the GPU super-buffer.
        std::unordered_map<ChunkKey, ChunkDraw, ChunkKeyHash> m_Chunks;

        // GPU resources (super-buffer)
        mutable unsigned int m_VAO = 0;
        mutable unsigned int m_VBO = 0;
        mutable unsigned int m_EBO = 0;
        mutable unsigned int m_SSBO = 0;

        uint32_t m_VertexBufferCapacity = 0;
        uint32_t m_IndexBufferCapacity = 0;

        std::vector<FreeBlock> m_FreeVertices;
        std::vector<FreeBlock> m_FreeIndices;

        // Indirect commands + SSBO data (kept compact; drawIndex indexes these)
        mutable std::vector<glm::vec4> m_DrawChunkPositions;
        mutable std::vector<DrawCommand> m_DrawCommands;

        mutable std::vector<glm::vec4> m_VisibleChunkPositions;
        mutable std::vector<DrawCommand> m_VisibleCommands;
        mutable bool m_DrawTablesDirty = true;

        mutable std::size_t m_LastDrawCount = 0;
        mutable std::size_t m_LastIndexCount = 0;
    };

}
