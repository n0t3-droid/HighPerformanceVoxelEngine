#include "Chunk.h"
#include <cstring>

namespace Game { namespace World {

    Chunk::Chunk(glm::ivec3 position) : m_Position(position), m_IsDirty(true) {
        // Initialize to 0 (Air)
        std::memset(m_Blocks, 0, sizeof(m_Blocks));
    }

    Chunk::~Chunk() {
    }

    uint8_t Chunk::GetBlock(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
            return 0; // Air
        }
        return m_Blocks[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE]; // x + y*w + z*w*h logic used in 1D
    }

    void Chunk::SetBlock(int x, int y, int z, uint8_t type) {
        if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) {
            return;
        }
        m_Blocks[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE] = type;
        m_IsDirty = true;
    }

    void Chunk::CopyBlocksTo(std::vector<uint8_t>& out) const {
        out.resize(BlockCount);
        std::memcpy(out.data(), m_Blocks, BlockCount);
    }

    void Chunk::CopyBlocksFrom(const uint8_t* data, std::size_t size) {
        if (!data || size < BlockCount) return;
        std::memcpy(m_Blocks, data, BlockCount);
        m_IsDirty = true;
    }

    void Chunk::CompressToRLESlices(std::vector<RLESlice>& out) const {
        out.clear();
        out.reserve(BlockCount / 2);

        std::size_t index = 0;
        while (index < BlockCount) {
            const uint8_t value = m_Blocks[index];
            uint16_t run = 1;
            while ((index + run) < BlockCount && m_Blocks[index + run] == value && run < 65535) {
                ++run;
            }

            out.push_back(RLESlice{ value, run });
            index += (std::size_t)run;
        }
    }

    bool Chunk::DecompressFromRLESlices(const std::vector<RLESlice>& in) {
        std::size_t index = 0;
        for (const auto& slice : in) {
            if (slice.count == 0) return false;
            if (index + (std::size_t)slice.count > BlockCount) return false;

            std::memset(m_Blocks + index, (int)slice.block, (std::size_t)slice.count);
            index += (std::size_t)slice.count;
        }

        if (index != BlockCount) return false;
        m_IsDirty = true;
        return true;
    }
}}
