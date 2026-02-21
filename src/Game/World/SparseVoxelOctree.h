#pragma once

#include <glm/glm.hpp>

#include <unordered_set>

namespace Game { namespace World {

    struct SVOChunkKey {
        glm::ivec3 coord{0};
        bool operator==(const SVOChunkKey& other) const { return coord == other.coord; }
    };

    struct SVOChunkKeyHash {
        std::size_t operator()(const SVOChunkKey& k) const noexcept {
            const uint32_t x = (uint32_t)k.coord.x;
            const uint32_t y = (uint32_t)k.coord.y;
            const uint32_t z = (uint32_t)k.coord.z;
            uint32_t h = 2166136261u;
            auto mix = [&](uint32_t v) {
                h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
            };
            mix(x); mix(y); mix(z);
            return (std::size_t)h;
        }
    };

    class SparseVoxelOctree {
    public:
        void Clear();
        void MarkChunkResident(const glm::ivec3& chunkCoord);
        void MarkChunkRemoved(const glm::ivec3& chunkCoord);
        bool IsChunkResident(const glm::ivec3& chunkCoord) const;
        std::size_t ResidentChunkCount() const;

    private:
        std::unordered_set<SVOChunkKey, SVOChunkKeyHash> m_ResidentChunks;
    };

}}