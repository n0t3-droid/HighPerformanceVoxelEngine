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
    
    // ---------------------------------------------------------------------------
    //  INVENTION: Per-Vertex Ambient Occlusion packed into the vertex word.
    //  Block type reduced to 7 bits (0-127, max used = 83) to free 2 AO bits.
    //
    //  Packed Layout (32 bits total):
    //  Bits 0-5    (6 bits): X position (0-63)
    //  Bits 6-11   (6 bits): Y position (0-63)
    //  Bits 12-17  (6 bits): Z position (0-63)
    //  Bits 18-20  (3 bits): Normal Index (0-5)
    //  Bit  21     (1 bit) : U coord (0-1)
    //  Bit  22     (1 bit) : V coord (0-1)
    //  Bits 23-29  (7 bits): Block Type (0-127)
    //  Bits 30-31  (2 bits): AO level (0=darkest, 3=brightest)
    // ---------------------------------------------------------------------------
    uint32_t PackVertex(int x, int y, int z, int normal, int u, int v, int blockType, int ao = 3) {
        uint32_t data = 0;
        data |= (uint32_t(x) & 63u) << 0;
        data |= (uint32_t(y) & 63u) << 6;
        data |= (uint32_t(z) & 63u) << 12;
        data |= (uint32_t(normal) & 7u) << 18;
        data |= (uint32_t(u) & 1u) << 21;
        data |= (uint32_t(v) & 1u) << 22;
        data |= (uint32_t(blockType) & 127u) << 23;
        data |= (uint32_t(ao) & 3u) << 30;
        return data;
    }

    // ---------------------------------------------------------------------------
    //  INVENTION: Classic Minecraft-style per-vertex AO.
    //  For a given face vertex corner, sample 3 adjacent blocks (side1, side2,
    //  corner diagonal). The AO level 0-3 determines shadow darkness:
    //    3 = fully lit, 2 = light shadow, 1 = medium shadow, 0 = deep crease.
    //  This adds enormous depth perception for zero extra draw-call cost.
    // ---------------------------------------------------------------------------
    static int ComputeVertexAO(bool side1, bool side2, bool corner) {
        if (side1 && side2) return 0;
        return 3 - (int)side1 - (int)side2 - (int)corner;
    }

    static bool IsTransparent(uint8_t id) { return id == 0 || id == 5 || id == 18 || id == 35 || (id >= 36 && id <= 51); }

    static bool ShouldRenderFace(uint8_t currentBlock, uint8_t neighborBlock) { if (neighborBlock == 0) return true; if (currentBlock == neighborBlock) return false; return IsTransparent(neighborBlock); }

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
                    if (y == Game::World::CHUNK_SIZE - 1 || ShouldRenderFace(block, chunk.GetBlock(x, y + 1, z))) {
                        emitQuad(
                            PackVertex(x,   y + 1, z,     0, 0, 0, block),
                            PackVertex(x,   y + 1, z + 1, 0, 0, 1, block),
                            PackVertex(x+1, y + 1, z + 1, 0, 1, 1, block),
                            PackVertex(x+1, y + 1, z,     0, 1, 0, block)
                        );
                    }

                    // DOWN
                    if (y == 0 || ShouldRenderFace(block, chunk.GetBlock(x, y - 1, z))) {
                        emitQuad(
                            PackVertex(x,   y, z + 1, 1, 0, 0, block),
                            PackVertex(x,   y, z,     1, 0, 1, block),
                            PackVertex(x+1, y, z,     1, 1, 1, block),
                            PackVertex(x+1, y, z + 1, 1, 1, 0, block)
                        );
                    }

                    // EAST
                    if (x == Game::World::CHUNK_SIZE - 1 || ShouldRenderFace(block, chunk.GetBlock(x + 1, y, z))) {
                        emitQuad(
                            PackVertex(x+1, y,   z,     4, 0, 0, block),
                            PackVertex(x+1, y+1, z,     4, 0, 1, block),
                            PackVertex(x+1, y+1, z + 1, 4, 1, 1, block),
                            PackVertex(x+1, y,   z + 1, 4, 1, 0, block)
                        );
                    }

                    // WEST
                    if (x == 0 || ShouldRenderFace(block, chunk.GetBlock(x - 1, y, z))) {
                        emitQuad(
                            PackVertex(x, y,   z + 1, 5, 0, 0, block),
                            PackVertex(x, y+1, z + 1, 5, 0, 1, block),
                            PackVertex(x, y+1, z,     5, 1, 1, block),
                            PackVertex(x, y,   z,     5, 1, 0, block)
                        );
                    }

                    // SOUTH
                    if (z == Game::World::CHUNK_SIZE - 1 || ShouldRenderFace(block, chunk.GetBlock(x, y, z + 1))) {
                        emitQuad(
                            PackVertex(x+1, y,   z + 1, 2, 0, 0, block),
                            PackVertex(x+1, y+1, z + 1, 2, 0, 1, block),
                            PackVertex(x,   y+1, z + 1, 2, 1, 1, block),
                            PackVertex(x,   y,   z + 1, 2, 1, 0, block)
                        );
                    }

                    // NORTH
                    if (z == 0 || ShouldRenderFace(block, chunk.GetBlock(x, y, z - 1))) {
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

    // ---------------------------------------------------------------------------
    //  INVENTION: High-performance mesher with per-vertex AO, face-count
    //  pre-estimation, and SIMD-style word-level empty detection.
    //
    //  Per-vertex AO samples 3 diagonal neighbors for each face corner
    //  (classic Minecraft style) and encodes the result in 2 bits per vertex.
    //  This produces gorgeous self-shadowing at ZERO extra GPU cost.
    //
    //  Face pre-estimation scans blocks once to count exposed faces, then
    //  pre-allocates vertex/index arrays to the exact size — eliminating
    //  all vector growth/realloc overhead during meshing.
    //
    //  Word-level empty scan checks 4 bytes at once to detect pure-air
    //  chunks in ~8K iterations instead of ~32K.
    // ---------------------------------------------------------------------------
    void BuildChunkMeshCPU_Padded(const uint8_t* paddedBlocks, int paddedSize, std::vector<uint32_t>& outVertices, std::vector<unsigned int>& outIndices) {
        outVertices.clear();
        outIndices.clear();
        if (!paddedBlocks || paddedSize < 34) return;

        constexpr int SIZE = Game::World::CHUNK_SIZE;
        const int pad = paddedSize;
        const int padSq = pad * pad;

        // INVENTION: Word-level solid scan — 4x fewer iterations than byte scan.
        {
            const uint32_t* words = reinterpret_cast<const uint32_t*>(paddedBlocks);
            const std::size_t wordCount = ((std::size_t)pad * (std::size_t)pad * (std::size_t)pad) / 4;
            bool hasAny = false;
            for (std::size_t i = 0; i < wordCount; ++i) {
                if (words[i] != 0u) { hasAny = true; break; }
            }
            // Check trailing bytes
            if (!hasAny) {
                const std::size_t bytesDone = wordCount * 4;
                const std::size_t totalBytes = (std::size_t)pad * (std::size_t)pad * (std::size_t)pad;
                for (std::size_t i = bytesDone; i < totalBytes; ++i) {
                    if (paddedBlocks[i] != 0) { hasAny = true; break; }
                }
            }
            if (!hasAny) return; // Entire volume is air — nothing to mesh.
        }

        // Inline block accessor using direct indexing (no bounds checks needed
        // because the padded volume guarantees 1-block border on all sides).
        auto getP = [&](int px, int py, int pz) -> uint8_t {
            return paddedBlocks[px + py * pad + pz * padSq];
        };
        auto get = [&](int lx, int ly, int lz) -> uint8_t {
            return getP(lx + 1, ly + 1, lz + 1);
        };
        // Bool solid test (for AO).
        auto solid = [&](int lx, int ly, int lz) -> bool {
            return getP(lx + 1, ly + 1, lz + 1) != 0;
        };

        // INVENTION: Face-count pre-estimation. Scan once to count exposed faces,
        // then allocate exact output buffer sizes. This eliminates all vector
        // growth overhead during the main meshing loop.
        std::size_t faceEstimate = 0;
        for (int x = 0; x < SIZE; ++x) {
            for (int y = 0; y < SIZE; ++y) {
                for (int z = 0; z < SIZE; ++z) {
                    if (get(x, y, z) == 0) continue;
                    if (get(x, y + 1, z) == 0) ++faceEstimate;
                    if (get(x, y - 1, z) == 0) ++faceEstimate;
                    if (get(x + 1, y, z) == 0) ++faceEstimate;
                    if (get(x - 1, y, z) == 0) ++faceEstimate;
                    if (get(x, y, z + 1) == 0) ++faceEstimate;
                    if (get(x, y, z - 1) == 0) ++faceEstimate;
                }
            }
        }
        if (faceEstimate == 0) return;

        outVertices.reserve(faceEstimate * 4);
        outIndices.reserve(faceEstimate * 6);

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

                    // UP (+Y)
                    if (get(x, y + 1, z) == 0) {
                        const int ao0 = ComputeVertexAO(solid(x - 1, y + 1, z), solid(x, y + 1, z - 1), solid(x - 1, y + 1, z - 1));
                        const int ao1 = ComputeVertexAO(solid(x - 1, y + 1, z), solid(x, y + 1, z + 1), solid(x - 1, y + 1, z + 1));
                        const int ao2 = ComputeVertexAO(solid(x + 1, y + 1, z), solid(x, y + 1, z + 1), solid(x + 1, y + 1, z + 1));
                        const int ao3 = ComputeVertexAO(solid(x + 1, y + 1, z), solid(x, y + 1, z - 1), solid(x + 1, y + 1, z - 1));
                        emitQuad(
                            PackVertex(x,   y + 1, z,     0, 0, 0, b, ao0),
                            PackVertex(x,   y + 1, z + 1, 0, 0, 1, b, ao1),
                            PackVertex(x+1, y + 1, z + 1, 0, 1, 1, b, ao2),
                            PackVertex(x+1, y + 1, z,     0, 1, 0, b, ao3)
                        );
                    }
                    // DOWN (-Y)
                    if (get(x, y - 1, z) == 0) {
                        const int ao0 = ComputeVertexAO(solid(x - 1, y - 1, z), solid(x, y - 1, z + 1), solid(x - 1, y - 1, z + 1));
                        const int ao1 = ComputeVertexAO(solid(x - 1, y - 1, z), solid(x, y - 1, z - 1), solid(x - 1, y - 1, z - 1));
                        const int ao2 = ComputeVertexAO(solid(x + 1, y - 1, z), solid(x, y - 1, z - 1), solid(x + 1, y - 1, z - 1));
                        const int ao3 = ComputeVertexAO(solid(x + 1, y - 1, z), solid(x, y - 1, z + 1), solid(x + 1, y - 1, z + 1));
                        emitQuad(
                            PackVertex(x,   y, z + 1, 1, 0, 0, b, ao0),
                            PackVertex(x,   y, z,     1, 0, 1, b, ao1),
                            PackVertex(x+1, y, z,     1, 1, 1, b, ao2),
                            PackVertex(x+1, y, z + 1, 1, 1, 0, b, ao3)
                        );
                    }
                    // EAST (+X)
                    if (get(x + 1, y, z) == 0) {
                        const int ao0 = ComputeVertexAO(solid(x + 1, y, z - 1), solid(x + 1, y - 1, z), solid(x + 1, y - 1, z - 1));
                        const int ao1 = ComputeVertexAO(solid(x + 1, y, z - 1), solid(x + 1, y + 1, z), solid(x + 1, y + 1, z - 1));
                        const int ao2 = ComputeVertexAO(solid(x + 1, y, z + 1), solid(x + 1, y + 1, z), solid(x + 1, y + 1, z + 1));
                        const int ao3 = ComputeVertexAO(solid(x + 1, y, z + 1), solid(x + 1, y - 1, z), solid(x + 1, y - 1, z + 1));
                        emitQuad(
                            PackVertex(x+1, y,   z,     4, 0, 0, b, ao0),
                            PackVertex(x+1, y+1, z,     4, 0, 1, b, ao1),
                            PackVertex(x+1, y+1, z + 1, 4, 1, 1, b, ao2),
                            PackVertex(x+1, y,   z + 1, 4, 1, 0, b, ao3)
                        );
                    }
                    // WEST (-X)
                    if (get(x - 1, y, z) == 0) {
                        const int ao0 = ComputeVertexAO(solid(x - 1, y, z + 1), solid(x - 1, y - 1, z), solid(x - 1, y - 1, z + 1));
                        const int ao1 = ComputeVertexAO(solid(x - 1, y, z + 1), solid(x - 1, y + 1, z), solid(x - 1, y + 1, z + 1));
                        const int ao2 = ComputeVertexAO(solid(x - 1, y, z - 1), solid(x - 1, y + 1, z), solid(x - 1, y + 1, z - 1));
                        const int ao3 = ComputeVertexAO(solid(x - 1, y, z - 1), solid(x - 1, y - 1, z), solid(x - 1, y - 1, z - 1));
                        emitQuad(
                            PackVertex(x, y,   z + 1, 5, 0, 0, b, ao0),
                            PackVertex(x, y+1, z + 1, 5, 0, 1, b, ao1),
                            PackVertex(x, y+1, z,     5, 1, 1, b, ao2),
                            PackVertex(x, y,   z,     5, 1, 0, b, ao3)
                        );
                    }
                    // SOUTH (+Z)
                    if (get(x, y, z + 1) == 0) {
                        const int ao0 = ComputeVertexAO(solid(x + 1, y, z + 1), solid(x, y - 1, z + 1), solid(x + 1, y - 1, z + 1));
                        const int ao1 = ComputeVertexAO(solid(x + 1, y, z + 1), solid(x, y + 1, z + 1), solid(x + 1, y + 1, z + 1));
                        const int ao2 = ComputeVertexAO(solid(x - 1, y, z + 1), solid(x, y + 1, z + 1), solid(x - 1, y + 1, z + 1));
                        const int ao3 = ComputeVertexAO(solid(x - 1, y, z + 1), solid(x, y - 1, z + 1), solid(x - 1, y - 1, z + 1));
                        emitQuad(
                            PackVertex(x+1, y,   z + 1, 2, 0, 0, b, ao0),
                            PackVertex(x+1, y+1, z + 1, 2, 0, 1, b, ao1),
                            PackVertex(x,   y+1, z + 1, 2, 1, 1, b, ao2),
                            PackVertex(x,   y,   z + 1, 2, 1, 0, b, ao3)
                        );
                    }
                    // NORTH (-Z)
                    if (get(x, y, z - 1) == 0) {
                        const int ao0 = ComputeVertexAO(solid(x - 1, y, z - 1), solid(x, y - 1, z - 1), solid(x - 1, y - 1, z - 1));
                        const int ao1 = ComputeVertexAO(solid(x - 1, y, z - 1), solid(x, y + 1, z - 1), solid(x - 1, y + 1, z - 1));
                        const int ao2 = ComputeVertexAO(solid(x + 1, y, z - 1), solid(x, y + 1, z - 1), solid(x + 1, y + 1, z - 1));
                        const int ao3 = ComputeVertexAO(solid(x + 1, y, z - 1), solid(x, y - 1, z - 1), solid(x + 1, y - 1, z - 1));
                        emitQuad(
                            PackVertex(x,   y,   z, 3, 0, 0, b, ao0),
                            PackVertex(x,   y+1, z, 3, 0, 1, b, ao1),
                            PackVertex(x+1, y+1, z, 3, 1, 1, b, ao2),
                            PackVertex(x+1, y,   z, 3, 1, 0, b, ao3)
                        );
                    }
                }
            }
        }

        std::cout << "[MESH+AO] " << (outVertices.size() / 4) << " Quads | " << outIndices.size() << " Indices | AO=2bit" << std::endl;
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

