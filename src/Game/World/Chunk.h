#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace Game { namespace World {

    constexpr int CHUNK_SIZE = 32;

    struct RLESlice {
        uint8_t block = 0;
        uint16_t count = 0;
    };

    class Chunk {
    public:
        Chunk(glm::ivec3 position);
        ~Chunk();

        // No copy
        Chunk(const Chunk&) = delete;
        Chunk& operator=(const Chunk&) = delete;

        uint8_t GetBlock(int x, int y, int z) const;
        void SetBlock(int x, int y, int z, uint8_t type);

        glm::ivec3 GetPosition() const { return m_Position; }
        
        bool IsDirty() const { return m_IsDirty; }
        void SetDirty(bool dirty) { m_IsDirty = dirty; }

        // Bulk block access for persistence.
        static constexpr std::size_t BlockCount = (std::size_t)CHUNK_SIZE * (std::size_t)CHUNK_SIZE * (std::size_t)CHUNK_SIZE;
        void CopyBlocksTo(std::vector<uint8_t>& out) const;
        void CopyBlocksFrom(const uint8_t* data, std::size_t size);

        void CompressToRLESlices(std::vector<RLESlice>& out) const;
        bool DecompressFromRLESlices(const std::vector<RLESlice>& in);

    private:
        glm::ivec3 m_Position;
        uint8_t m_Blocks[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];
        bool m_IsDirty;
    };
}}
