#include "Engine/Renderer/Mesh.h"
#include "Game/World/Chunk.h"
#include <iostream>
#include <cassert>

namespace Engine {
    struct AtlasUVRect {
        float uStart = 0.0f;
        float vStart = 0.0f;
        float uEnd = 0.0f;
        float vEnd = 0.0f;
    };

    static AtlasUVRect ComputeAtlasUVRect(uint16_t blockId) {
        // Minecraft-style atlas math on a 16x16 tile grid.
        // n is 0-based tile index (blockId==0 means empty).
        constexpr int kAtlasGrid = 16;
        constexpr float kInvAtlasGrid = 1.0f / 16.0f;
        constexpr float kEpsilon = 0.0005f;

        if (blockId == 0) {
            return AtlasUVRect{};
        }

        const int n = int(blockId) - 1;
        const float uStart = float(n % kAtlasGrid) * kInvAtlasGrid;
        const float vStart = float(n / kAtlasGrid) * kInvAtlasGrid;
        const float uEnd = uStart + kInvAtlasGrid;
        const float vEnd = vStart + kInvAtlasGrid;

        return AtlasUVRect{
            uStart + kEpsilon,
            vStart + kEpsilon,
            uEnd - kEpsilon,
            vEnd - kEpsilon
        };
    }

    Mesh::Mesh() : m_IndexCount(0) {
        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glGenBuffers(1, &m_EBO);

        glBindVertexArray(m_VAO);

        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        // Attribute 0: uint32_t (Packed data)
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(uint32_t), (void*)0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);

        glBindVertexArray(0);
    }

    Mesh::~Mesh() {
        glDeleteVertexArrays(1, &m_VAO);
        glDeleteBuffers(1, &m_VBO);
        glDeleteBuffers(1, &m_EBO);
    }

    void Mesh::AddFace(uint32_t packedData) {
        m_Vertices.push_back(packedData);
    }

    void Mesh::Clear() {
        m_Vertices.clear();
        m_Indices.clear();
        m_IndexCount = 0;
    }

    void Mesh::Upload() {
        if (m_Vertices.empty()) return;

        glBindVertexArray(m_VAO); // Must bind VAO to associate EBO

        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, m_Vertices.size() * sizeof(uint32_t), m_Vertices.data(), GL_DYNAMIC_DRAW);
        
        // Generate indices for quads
        size_t faceCount = m_Vertices.size() / 4; 
        m_Indices.clear();
        m_Indices.reserve(faceCount * 6);
        for (size_t i = 0; i < faceCount; i++) {
            unsigned int offset = static_cast<unsigned int>(i * 4);
            m_Indices.push_back(offset + 0);
            m_Indices.push_back(offset + 1);
            m_Indices.push_back(offset + 2);
            m_Indices.push_back(offset + 2);
            m_Indices.push_back(offset + 3);
            m_Indices.push_back(offset + 0);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Indices.size() * sizeof(unsigned int), m_Indices.data(), GL_DYNAMIC_DRAW);
        
        m_IndexCount = m_Indices.size();
        
        glBindVertexArray(0); // Unbind
    }

    void Mesh::Draw() const {
        if (m_IndexCount == 0) return;
        
        glBindVertexArray(m_VAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_IndexCount), GL_UNSIGNED_INT, 0);
    }

    // --- Meshing Logic ---
    
    // Bit Packing Helper - includes block type for texture selection
    uint32_t PackVertex(int x, int y, int z, int normal, int u, int v, int blockType) {
        // Packed Layout (32 bits total):
        // We must support coordinates on voxel cell corners, i.e. 0..32 for CHUNK_SIZE=32.
        // Bits 0-5    (6 bits): X position (0-63)
        // Bits 6-11   (6 bits): Y position (0-63)
        // Bits 12-17  (6 bits): Z position (0-63)
        // Bits 18-20  (3 bits): Normal Index (0-5)
        // Bit  21     (1 bit) : U coord (0-1)
        // Bit  22     (1 bit) : V coord (0-1)
        // Bits 23-30  (8 bits): Block Type (0-255)
        // Bit  31     (1 bit) : Reserved (AO, etc)
        
        uint32_t data = 0;
        data |= (uint32_t(x) & 63u) << 0;
        data |= (uint32_t(y) & 63u) << 6;
        data |= (uint32_t(z) & 63u) << 12;
        data |= (uint32_t(normal) & 7u) << 18;
        data |= (uint32_t(u) & 1u) << 21;
        data |= (uint32_t(v) & 1u) << 22;
        data |= (uint32_t(blockType) & 255u) << 23;
        return data;
    }

    static bool IsPaneBlock(uint8_t id) {
        return id == 35 || (id >= 36 && id <= 51);
    }

    static bool PaneConnectsTo(uint8_t id) {
        if (id == 0 || id == 5) return false;
        return true;
    }

    void BuildChunkMeshCPU(const Game::World::Chunk& chunk, std::vector<uint32_t>& outVertices, std::vector<unsigned int>& outIndices) {
        outVertices.clear();
        outIndices.clear();

        // Naive face-culling meshing (single chunk only, no cross-chunk neighbors yet)
        for (int x = 0; x < Game::World::CHUNK_SIZE; x++) {
            for (int y = 0; y < Game::World::CHUNK_SIZE; y++) {
                for (int z = 0; z < Game::World::CHUNK_SIZE; z++) {
                    uint8_t block = chunk.GetBlock(x, y, z);
                    if (block == 0) continue;

                    auto emitQuad = [&](uint32_t v0, uint32_t v1, uint32_t v2, uint32_t v3) {
                        const unsigned int base = (unsigned int)outVertices.size();
                        outVertices.push_back(v0);
                        outVertices.push_back(v1);
                        outVertices.push_back(v2);
                        outVertices.push_back(v3);

                        outIndices.push_back(base + 0);
                        outIndices.push_back(base + 1);
                        outIndices.push_back(base + 2);
                        outIndices.push_back(base + 2);
                        outIndices.push_back(base + 3);
                        outIndices.push_back(base + 0);
                    };

                    if (IsPaneBlock(block)) {
                        const uint8_t n = (z > 0) ? chunk.GetBlock(x, y, z - 1) : 0;
                        const uint8_t s = (z < Game::World::CHUNK_SIZE - 1) ? chunk.GetBlock(x, y, z + 1) : 0;
                        const uint8_t w = (x > 0) ? chunk.GetBlock(x - 1, y, z) : 0;
                        const uint8_t e = (x < Game::World::CHUNK_SIZE - 1) ? chunk.GetBlock(x + 1, y, z) : 0;

                        bool connectX = PaneConnectsTo(w) || PaneConnectsTo(e);
                        bool connectZ = PaneConnectsTo(n) || PaneConnectsTo(s);
                        if (!connectX && !connectZ) {
                            connectX = true;
                            connectZ = true;
                        }

                        if (connectX) {
                            // EAST
                            emitQuad(
                                PackVertex(x+1, y,   z,     4, 0, 0, block),
                                PackVertex(x+1, y+1, z,     4, 0, 1, block),
                                PackVertex(x+1, y+1, z + 1, 4, 1, 1, block),
                                PackVertex(x+1, y,   z + 1, 4, 1, 0, block)
                            );
                            // WEST
                            emitQuad(
                                PackVertex(x, y,   z + 1, 5, 0, 0, block),
                                PackVertex(x, y+1, z + 1, 5, 0, 1, block),
                                PackVertex(x, y+1, z,     5, 1, 1, block),
                                PackVertex(x, y,   z,     5, 1, 0, block)
                            );
                        }

                        if (connectZ) {
                            // SOUTH
                            emitQuad(
                                PackVertex(x+1, y,   z + 1, 2, 0, 0, block),
                                PackVertex(x+1, y+1, z + 1, 2, 0, 1, block),
                                PackVertex(x,   y+1, z + 1, 2, 1, 1, block),
                                PackVertex(x,   y,   z + 1, 2, 1, 0, block)
                            );
                            // NORTH
                            emitQuad(
                                PackVertex(x,   y,   z, 3, 0, 0, block),
                                PackVertex(x,   y+1, z, 3, 0, 1, block),
                                PackVertex(x+1, y+1, z, 3, 1, 1, block),
                                PackVertex(x+1, y,   z, 3, 1, 0, block)
                            );
                        }

                        continue;
                    }

                    // UP
                    if (y == Game::World::CHUNK_SIZE - 1 || chunk.GetBlock(x, y + 1, z) == 0) {
                        emitQuad(
                            PackVertex(x,   y + 1, z,     0, 0, 0, block),
                            PackVertex(x,   y + 1, z + 1, 0, 0, 1, block),
                            PackVertex(x+1, y + 1, z + 1, 0, 1, 1, block),
                            PackVertex(x+1, y + 1, z,     0, 1, 0, block)
                        );
                    }

                    // DOWN
                    if (y == 0 || chunk.GetBlock(x, y - 1, z) == 0) {
                        emitQuad(
                            PackVertex(x,   y, z + 1, 1, 0, 0, block),
                            PackVertex(x,   y, z,     1, 0, 1, block),
                            PackVertex(x+1, y, z,     1, 1, 1, block),
                            PackVertex(x+1, y, z + 1, 1, 1, 0, block)
                        );
                    }

                    // EAST
                    if (x == Game::World::CHUNK_SIZE - 1 || chunk.GetBlock(x + 1, y, z) == 0) {
                        emitQuad(
                            PackVertex(x+1, y,   z,     4, 0, 0, block),
                            PackVertex(x+1, y+1, z,     4, 0, 1, block),
                            PackVertex(x+1, y+1, z + 1, 4, 1, 1, block),
                            PackVertex(x+1, y,   z + 1, 4, 1, 0, block)
                        );
                    }

                    // WEST
                    if (x == 0 || chunk.GetBlock(x - 1, y, z) == 0) {
                        emitQuad(
                            PackVertex(x, y,   z + 1, 5, 0, 0, block),
                            PackVertex(x, y+1, z + 1, 5, 0, 1, block),
                            PackVertex(x, y+1, z,     5, 1, 1, block),
                            PackVertex(x, y,   z,     5, 1, 0, block)
                        );
                    }

                    // SOUTH
                    if (z == Game::World::CHUNK_SIZE - 1 || chunk.GetBlock(x, y, z + 1) == 0) {
                        emitQuad(
                            PackVertex(x+1, y,   z + 1, 2, 0, 0, block),
                            PackVertex(x+1, y+1, z + 1, 2, 0, 1, block),
                            PackVertex(x,   y+1, z + 1, 2, 1, 1, block),
                            PackVertex(x,   y,   z + 1, 2, 1, 0, block)
                        );
                    }

                    // NORTH
                    if (z == 0 || chunk.GetBlock(x, y, z - 1) == 0) {
                        emitQuad(
                            PackVertex(x,   y,   z, 3, 0, 0, block),
                            PackVertex(x,   y+1, z, 3, 0, 1, block),
                            PackVertex(x+1, y+1, z, 3, 1, 1, block),
                            PackVertex(x+1, y,   z, 3, 1, 0, block)
                        );
                    }
                }
            }
        }
    }

    void BuildChunkMeshCPU_Padded(const uint8_t* paddedBlocks, int paddedSize, std::vector<uint32_t>& outVertices, std::vector<unsigned int>& outIndices) {
        outVertices.clear();
        outIndices.clear();
        if (!paddedBlocks || paddedSize < 34) return;

        constexpr int SIZE = Game::World::CHUNK_SIZE;
        const int pad = paddedSize;

        auto get = [&](int lx, int ly, int lz) -> uint8_t {
            const int px = lx + 1;
            const int py = ly + 1;
            const int pz = lz + 1;
            if (px < 0 || px >= pad || py < 0 || py >= pad || pz < 0 || pz >= pad) return 0;
            return paddedBlocks[px + py * pad + pz * pad * pad];
        };

        auto emitQuad = [&](uint32_t v0, uint32_t v1, uint32_t v2, uint32_t v3) {
            const unsigned int base = (unsigned int)outVertices.size();
            outVertices.push_back(v0);
            outVertices.push_back(v1);
            outVertices.push_back(v2);
            outVertices.push_back(v3);
            outIndices.push_back(base + 0); outIndices.push_back(base + 1); outIndices.push_back(base + 2);
            outIndices.push_back(base + 0); outIndices.push_back(base + 2); outIndices.push_back(base + 3);
        };

        for (int x = 0; x < SIZE; ++x) {
            for (int y = 0; y < SIZE; ++y) {
                for (int z = 0; z < SIZE; ++z) {
                    const uint8_t b = get(x, y, z);
                    if (b == 0) continue;

                    if (get(x, y + 1, z) == 0) {
                        emitQuad(
                            PackVertex(x,   y + 1, z,     0, 0, 0, b),
                            PackVertex(x,   y + 1, z + 1, 0, 0, 1, b),
                            PackVertex(x+1, y + 1, z + 1, 0, 1, 1, b),
                            PackVertex(x+1, y + 1, z,     0, 1, 0, b)
                        );
                    }
                    if (get(x, y - 1, z) == 0) {
                        emitQuad(
                            PackVertex(x,   y, z + 1, 1, 0, 0, b),
                            PackVertex(x,   y, z,     1, 0, 1, b),
                            PackVertex(x+1, y, z,     1, 1, 1, b),
                            PackVertex(x+1, y, z + 1, 1, 1, 0, b)
                        );
                    }
                    if (get(x + 1, y, z) == 0) {
                        emitQuad(
                            PackVertex(x+1, y,   z,     4, 0, 0, b),
                            PackVertex(x+1, y+1, z,     4, 0, 1, b),
                            PackVertex(x+1, y+1, z + 1, 4, 1, 1, b),
                            PackVertex(x+1, y,   z + 1, 4, 1, 0, b)
                        );
                    }
                    if (get(x - 1, y, z) == 0) {
                        emitQuad(
                            PackVertex(x, y,   z + 1, 5, 0, 0, b),
                            PackVertex(x, y+1, z + 1, 5, 0, 1, b),
                            PackVertex(x, y+1, z,     5, 1, 1, b),
                            PackVertex(x, y,   z,     5, 1, 0, b)
                        );
                    }
                    if (get(x, y, z + 1) == 0) {
                        emitQuad(
                            PackVertex(x+1, y,   z + 1, 2, 0, 0, b),
                            PackVertex(x+1, y+1, z + 1, 2, 0, 1, b),
                            PackVertex(x,   y+1, z + 1, 2, 1, 1, b),
                            PackVertex(x,   y,   z + 1, 2, 1, 0, b)
                        );
                    }
                    if (get(x, y, z - 1) == 0) {
                        emitQuad(
                            PackVertex(x,   y,   z, 3, 0, 0, b),
                            PackVertex(x,   y+1, z, 3, 0, 1, b),
                            PackVertex(x+1, y+1, z, 3, 1, 1, b),
                            PackVertex(x+1, y,   z, 3, 1, 0, b)
                        );
                    }
                }
            }
        }

        std::cout << "[MESH SUCCESS] " << (outVertices.size() / 4) << " Quads | " << outIndices.size() << " Indices" << std::endl;
    }

    void UploadMeshToGPU(Mesh& mesh, const std::vector<uint32_t>& vertices, const std::vector<unsigned int>& indices) {
        mesh.Clear();
        mesh.m_Vertices = vertices;
        mesh.m_Indices = indices;
        mesh.m_IndexCount = mesh.m_Indices.size();

        if (mesh.m_Vertices.empty() || mesh.m_Indices.empty()) return;

        glBindVertexArray(mesh.m_VAO);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.m_VBO);
        glBufferData(GL_ARRAY_BUFFER, mesh.m_Vertices.size() * sizeof(uint32_t), mesh.m_Vertices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.m_Indices.size() * sizeof(unsigned int), mesh.m_Indices.data(), GL_DYNAMIC_DRAW);

        glBindVertexArray(0);
    }

    void MeshChunk(const Game::World::Chunk& chunk, Mesh& mesh) {
        std::vector<uint32_t> verts;
        std::vector<unsigned int> inds;
        BuildChunkMeshCPU(chunk, verts, inds);
        UploadMeshToGPU(mesh, verts, inds);
    }
}
