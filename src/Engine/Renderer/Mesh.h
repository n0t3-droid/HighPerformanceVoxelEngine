#pragma once

#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

namespace Engine {

    struct Vertex {
        // Packed data:
        // x, y, z : 5 bits each? No, we need more for local chunk pos? 
        // 0-31 range is fine for chunk relative.
        // But for Mesh, we might want generic float for now to verify?
        // No, user demanded Bit Packing.
        // "Pack data into a single uint32_t integer"
        uint32_t data;
    };

    class Mesh {
    public:
        Mesh();
        ~Mesh();

        void AddFace(uint32_t packedData); // For now, simple
        void Clear();
        void Upload();
        void Draw() const; // Direct draw for testing

        // Core Members
        std::vector<uint32_t> m_Vertices;
        std::vector<unsigned int> m_Indices;
        
        unsigned int m_VAO, m_VBO, m_EBO;
        size_t m_IndexCount;
    };

    // Forward declaration
}

namespace Game { namespace World { class Chunk; } }

namespace Engine {
    // Worker-thread safe: builds packed vertices + indices in CPU memory.
    void BuildChunkMeshCPU(const Game::World::Chunk& chunk, std::vector<uint32_t>& outVertices, std::vector<unsigned int>& outIndices);

    // Worker-thread safe: builds from a padded (CHUNK_SIZE+2)^3 block volume.
    // Padded coordinates (x+1,y+1,z+1) correspond to chunk-local voxel (x,y,z).
    // This allows neighbor-aware face culling at chunk borders.
    void BuildChunkMeshCPU_Padded(const uint8_t* paddedBlocks, int paddedSize, std::vector<uint32_t>& outVertices, std::vector<unsigned int>& outIndices);

    // Main-thread (OpenGL) upload helpers.
    void UploadMeshToGPU(Mesh& mesh, const std::vector<uint32_t>& vertices, const std::vector<unsigned int>& indices);

    // Convenience: builds CPU mesh then uploads to the provided Mesh.
    void MeshChunk(const Game::World::Chunk& chunk, Mesh& mesh);
}
