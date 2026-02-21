#include "Game/World/SparseVoxelOctree.h"

namespace Game { namespace World {

    void SparseVoxelOctree::Clear() {
        m_ResidentChunks.clear();
    }

    void SparseVoxelOctree::MarkChunkResident(const glm::ivec3& chunkCoord) {
        m_ResidentChunks.insert(SVOChunkKey{ chunkCoord });
    }

    void SparseVoxelOctree::MarkChunkRemoved(const glm::ivec3& chunkCoord) {
        m_ResidentChunks.erase(SVOChunkKey{ chunkCoord });
    }

    bool SparseVoxelOctree::IsChunkResident(const glm::ivec3& chunkCoord) const {
        return m_ResidentChunks.find(SVOChunkKey{ chunkCoord }) != m_ResidentChunks.end();
    }

    std::size_t SparseVoxelOctree::ResidentChunkCount() const {
        return m_ResidentChunks.size();
    }

}}