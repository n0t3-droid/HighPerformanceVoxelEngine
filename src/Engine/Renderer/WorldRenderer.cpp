#include "Engine/Renderer/WorldRenderer.h"

#include "Engine/Renderer/Shader.h"
#include "Engine/Renderer/IndirectBuffer.h"

#include "Game/World/Chunk.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <limits>
#include <array>
#include <cmath>

#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif

// GL 4.3 entrypoint; GLAD in this repo is generated only up to 4.1.
typedef void (APIENTRYP PFNGLMULTIDRAWELEMENTSINDIRECTPROC)(GLenum mode, GLenum type, const void* indirect, GLsizei drawcount, GLsizei stride);
static PFNGLMULTIDRAWELEMENTSINDIRECTPROC pglMultiDrawElementsIndirect = nullptr;

namespace Engine {

    namespace {
        struct FrustumPlane {
            glm::vec3 n{0.0f};
            float d = 0.0f;
        };

        static glm::vec4 GetRow(const glm::mat4& m, int row) {
            return glm::vec4(m[0][row], m[1][row], m[2][row], m[3][row]);
        }

        static FrustumPlane NormalizePlane(const glm::vec4& p) {
            const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len <= 1e-6f) {
                return FrustumPlane{};
            }
            return FrustumPlane{glm::vec3(p.x, p.y, p.z) / len, p.w / len};
        }

        static std::array<FrustumPlane, 6> ExtractFrustumPlanes(const glm::mat4& vp) {
            const glm::vec4 r0 = GetRow(vp, 0);
            const glm::vec4 r1 = GetRow(vp, 1);
            const glm::vec4 r2 = GetRow(vp, 2);
            const glm::vec4 r3 = GetRow(vp, 3);

            return {
                NormalizePlane(r3 + r0), // left
                NormalizePlane(r3 - r0), // right
                NormalizePlane(r3 + r1), // bottom
                NormalizePlane(r3 - r1), // top
                NormalizePlane(r3 + r2), // near
                NormalizePlane(r3 - r2)  // far
            };
        }

        static bool AabbIntersectsFrustum(const glm::vec3& aabbMin,
                                          const glm::vec3& aabbMax,
                                          const std::array<FrustumPlane, 6>& planes) {
            for (const auto& p : planes) {
                glm::vec3 v = aabbMin;
                if (p.n.x >= 0.0f) v.x = aabbMax.x;
                if (p.n.y >= 0.0f) v.y = aabbMax.y;
                if (p.n.z >= 0.0f) v.z = aabbMax.z;

                if (glm::dot(p.n, v) + p.d < 0.0f) {
                    return false;
                }
            }
            return true;
        }
    }

    static IndirectBuffer& GetIndirect() {
        static IndirectBuffer s_Indirect;
        return s_Indirect;
    }

    WorldRenderer::WorldRenderer() {
        if (!pglMultiDrawElementsIndirect) {
            pglMultiDrawElementsIndirect = (PFNGLMULTIDRAWELEMENTSINDIRECTPROC)glfwGetProcAddress("glMultiDrawElementsIndirect");
        }

        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glGenBuffers(1, &m_EBO);
        glGenBuffers(1, &m_SSBO);

        glBindVertexArray(m_VAO);

        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(uint32_t), (void*)0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBindVertexArray(0);

        // Start with a larger capacity to support higher view distances with fewer reallocations.
        // 1u << 23 = 8,388,608 entries (~32MB each buffer).
        m_VertexBufferCapacity = 1u << 23;
        m_IndexBufferCapacity = 1u << 23;

        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, (size_t)m_VertexBufferCapacity * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (size_t)m_IndexBufferCapacity * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        m_FreeVertices = { FreeBlock{0u, m_VertexBufferCapacity} };
        m_FreeIndices = { FreeBlock{0u, m_IndexBufferCapacity} };
    }

    WorldRenderer::~WorldRenderer() {
        m_Chunks.clear();

        if (m_SSBO) glDeleteBuffers(1, &m_SSBO);
        if (m_EBO) glDeleteBuffers(1, &m_EBO);
        if (m_VBO) glDeleteBuffers(1, &m_VBO);
        if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    }

    uint32_t WorldRenderer::Alloc(std::vector<FreeBlock>& freeList, uint32_t size) {
        if (size == 0) return 0;
        for (size_t i = 0; i < freeList.size(); ++i) {
            FreeBlock& b = freeList[i];
            if (b.size < size) continue;
            const uint32_t off = b.offset;
            b.offset += size;
            b.size -= size;
            if (b.size == 0) {
                freeList.erase(freeList.begin() + (ptrdiff_t)i);
            }
            return off;
        }
        return std::numeric_limits<uint32_t>::max();
    }

    void WorldRenderer::Free(std::vector<FreeBlock>& freeList, uint32_t offset, uint32_t size) {
        if (size == 0) return;
        freeList.push_back(FreeBlock{offset, size});
        MergeFreeList(freeList);
    }

    void WorldRenderer::MergeFreeList(std::vector<FreeBlock>& freeList) {
        if (freeList.empty()) return;
        std::sort(freeList.begin(), freeList.end(), [](const FreeBlock& a, const FreeBlock& b) { return a.offset < b.offset; });
        size_t w = 0;
        for (size_t r = 0; r < freeList.size(); ++r) {
            if (w == 0) {
                freeList[w++] = freeList[r];
                continue;
            }
            FreeBlock& prev = freeList[w - 1];
            const FreeBlock& cur = freeList[r];
            if (prev.offset + prev.size == cur.offset) {
                prev.size += cur.size;
            } else {
                freeList[w++] = cur;
            }
        }
        freeList.resize(w);
    }

    void WorldRenderer::EnsureCapacity(uint32_t neededVerts, uint32_t neededIndices, uint32_t neededDraws) {
        bool grewBuffers = false;

        auto growPow2 = [](uint32_t v, uint32_t need) {
            if (v == 0) v = 1;
            while (v < need) v <<= 1;
            return v;
        };

        const uint32_t wantVerts = growPow2(m_VertexBufferCapacity, neededVerts);
        const uint32_t wantIdx = growPow2(m_IndexBufferCapacity, neededIndices);

        if (wantVerts != m_VertexBufferCapacity) {
            m_VertexBufferCapacity = wantVerts;
            grewBuffers = true;
        }
        if (wantIdx != m_IndexBufferCapacity) {
            m_IndexBufferCapacity = wantIdx;
            grewBuffers = true;
        }

        if ((uint32_t)m_DrawCommands.size() < neededDraws) {
            // draw tables are compact vectors; just resize
            m_DrawCommands.resize(neededDraws);
            m_DrawChunkPositions.resize(neededDraws);
        }

        if (grewBuffers) {
            glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
            glBufferData(GL_ARRAY_BUFFER, (size_t)m_VertexBufferCapacity * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (size_t)m_IndexBufferCapacity * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

            m_FreeVertices = { FreeBlock{0u, m_VertexBufferCapacity} };
            m_FreeIndices = { FreeBlock{0u, m_IndexBufferCapacity} };

            // Force reupload of all chunks into the new buffers.
            for (auto& kv : m_Chunks) {
                kv.second.hasGPU = false;
                kv.second.vertexOffset = kv.second.indexOffset = 0;
                kv.second.vertexCapacity = kv.second.indexCapacity = 0;
            }
            ReuploadAllGPU();
        }
    }

    void WorldRenderer::ReuploadAllGPU() {
        // Rebuild draw tables compactly.
        m_DrawCommands.clear();
        m_DrawChunkPositions.clear();
        m_DrawCommands.reserve(m_Chunks.size());
        m_DrawChunkPositions.reserve(m_Chunks.size());

        uint32_t drawIndex = 0;
        for (auto& kv : m_Chunks) {
            ChunkDraw& chunk = kv.second;
            chunk.drawIndex = drawIndex;
            UploadOrResizeChunkGPU(chunk);
            ++drawIndex;
        }

        // Upload SSBO and indirect buffer tables.
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     m_DrawChunkPositions.size() * sizeof(glm::vec4),
                     m_DrawChunkPositions.empty() ? nullptr : m_DrawChunkPositions.data(),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        GetIndirect().Upload(m_DrawCommands);
        UpdateStats();
        m_DrawTablesDirty = false;
    }

    void WorldRenderer::UploadOrResizeChunkGPU(ChunkDraw& chunk) {
        const uint32_t vCount = (uint32_t)chunk.vertices.size();
        const uint32_t iCount = (uint32_t)chunk.indices.size();

        if (vCount == 0 || iCount == 0) {
            // keep an empty draw command
            if (chunk.drawIndex >= m_DrawCommands.size()) {
                m_DrawCommands.resize(chunk.drawIndex + 1);
                m_DrawChunkPositions.resize(chunk.drawIndex + 1);
            }
            m_DrawCommands[chunk.drawIndex] = DrawCommand{0, 0, 0, 0, 0};
            m_DrawChunkPositions[chunk.drawIndex] = glm::vec4(glm::vec3(chunk.coord * Game::World::CHUNK_SIZE), 0.0f);
            return;
        }

        const uint32_t neededVerts = vCount;
        const uint32_t neededIdx = iCount;

        EnsureCapacity(neededVerts + 1, neededIdx + 1, (uint32_t)std::max(m_DrawCommands.size(), (size_t)chunk.drawIndex + 1));

        // Allocate or grow if needed
        if (!chunk.hasGPU || chunk.vertexCapacity < neededVerts || chunk.indexCapacity < neededIdx) {
            if (chunk.hasGPU) {
                Free(m_FreeVertices, chunk.vertexOffset, chunk.vertexCapacity);
                Free(m_FreeIndices, chunk.indexOffset, chunk.indexCapacity);
            }

            uint32_t vOff = Alloc(m_FreeVertices, neededVerts);
            uint32_t iOff = Alloc(m_FreeIndices, neededIdx);

            // If allocation failed, grow and retry via a full reupload.
            if (vOff == std::numeric_limits<uint32_t>::max() || iOff == std::numeric_limits<uint32_t>::max()) {
                const uint32_t growVerts = std::max(m_VertexBufferCapacity * 2u, m_VertexBufferCapacity + neededVerts + 1u);
                const uint32_t growIdx = std::max(m_IndexBufferCapacity * 2u, m_IndexBufferCapacity + neededIdx + 1u);
                m_VertexBufferCapacity = growVerts;
                m_IndexBufferCapacity = growIdx;

                glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
                glBufferData(GL_ARRAY_BUFFER, (size_t)m_VertexBufferCapacity * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, (size_t)m_IndexBufferCapacity * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

                m_FreeVertices = { FreeBlock{0u, m_VertexBufferCapacity} };
                m_FreeIndices = { FreeBlock{0u, m_IndexBufferCapacity} };
                for (auto& kv : m_Chunks) {
                    kv.second.hasGPU = false;
                    kv.second.vertexCapacity = kv.second.indexCapacity = 0;
                }
                ReuploadAllGPU();
                return;
            }

            chunk.vertexOffset = vOff;
            chunk.vertexCapacity = neededVerts;
            chunk.indexOffset = iOff;
            chunk.indexCapacity = neededIdx;
            chunk.hasGPU = true;
        }

        chunk.vertexCount = vCount;
        chunk.indexCount = iCount;

        // Ensure draw table vectors are large enough.
        if (chunk.drawIndex >= m_DrawCommands.size()) {
            m_DrawCommands.resize(chunk.drawIndex + 1);
            m_DrawChunkPositions.resize(chunk.drawIndex + 1);
        }

        // Upload chunk vertices
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferSubData(GL_ARRAY_BUFFER,
                        (GLintptr)((size_t)chunk.vertexOffset * sizeof(uint32_t)),
                        (GLsizeiptr)((size_t)chunk.vertexCount * sizeof(uint32_t)),
                        chunk.vertices.data());

        // Upload chunk indices
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                        (GLintptr)((size_t)chunk.indexOffset * sizeof(unsigned int)),
                        (GLsizeiptr)((size_t)chunk.indexCount * sizeof(unsigned int)),
                        chunk.indices.data());

        // Update SSBO position and draw command
        m_DrawChunkPositions[chunk.drawIndex] = glm::vec4(glm::vec3(chunk.coord * Game::World::CHUNK_SIZE), 0.0f);
        m_DrawCommands[chunk.drawIndex] = DrawCommand{
            chunk.indexCount,
            1,
            chunk.indexOffset,
            chunk.vertexOffset,
            0
        };
    }

    void WorldRenderer::UpdateStats() const {
        m_LastDrawCount = 0;
        m_LastIndexCount = 0;
        for (const auto& cmd : m_DrawCommands) {
            if (cmd.count == 0 || cmd.instanceCount == 0) continue;
            m_LastDrawCount += 1;
            m_LastIndexCount += cmd.count;
        }
    }

    void WorldRenderer::UploadOrUpdateChunk(const glm::ivec3& chunkCoord,
                                           const std::vector<uint32_t>& vertices,
                                           const std::vector<unsigned int>& indices) {
        ChunkKey key{chunkCoord};
        auto it = m_Chunks.find(key);
        if (it == m_Chunks.end()) {
            ChunkDraw draw;
            draw.coord = chunkCoord;
            draw.vertices = vertices;
            draw.indices = indices;

            // Allocate a draw slot by appending.
            draw.drawIndex = (uint32_t)m_DrawCommands.size();
            m_DrawCommands.push_back(DrawCommand{});
            m_DrawChunkPositions.push_back(glm::vec4(0.0f));

            auto [newIt, inserted] = m_Chunks.emplace(key, std::move(draw));
            (void)inserted;
            UploadOrResizeChunkGPU(newIt->second);
        } else {
            it->second.vertices = vertices;
            it->second.indices = indices;
            UploadOrResizeChunkGPU(it->second);
        }

        // Mark tables dirty; they are uploaded in the per-frame visibility prep.
        m_DrawTablesDirty = true;
        UpdateStats();
    }

    void WorldRenderer::RemoveChunk(const glm::ivec3& chunkCoord) {
        ChunkKey key{chunkCoord};
        auto it = m_Chunks.find(key);
        if (it == m_Chunks.end()) return;

        ChunkDraw& chunk = it->second;
        if (chunk.hasGPU) {
            Free(m_FreeVertices, chunk.vertexOffset, chunk.vertexCapacity);
            Free(m_FreeIndices, chunk.indexOffset, chunk.indexCapacity);
        }

        // Keep draw tables compact by swapping the last draw into this slot.
        const uint32_t removedIndex = chunk.drawIndex;
        const uint32_t lastIndex = (uint32_t)m_DrawCommands.size() - 1u;

        if (removedIndex != lastIndex) {
            m_DrawCommands[removedIndex] = m_DrawCommands[lastIndex];
            m_DrawChunkPositions[removedIndex] = m_DrawChunkPositions[lastIndex];

            // Fix the chunk that used to be at lastIndex.
            for (auto& kv : m_Chunks) {
                if (kv.second.drawIndex == lastIndex) {
                    kv.second.drawIndex = removedIndex;
                    break;
                }
            }
        }

        m_DrawCommands.pop_back();
        m_DrawChunkPositions.pop_back();

        m_Chunks.erase(it);

        // Mark tables dirty; they are uploaded in the per-frame visibility prep.
        m_DrawTablesDirty = true;
        UpdateStats();
    }

    void WorldRenderer::DrawAll(Shader& shader) const {
        IndirectBuffer& indirect = GetIndirect();
        const size_t drawCount = indirect.GetCommandCount();
        if (drawCount == 0) return;

        if (!pglMultiDrawElementsIndirect) return;

        shader.SetBool("u_UseDrawId", true);
        shader.SetVec3("u_ChunkPos", glm::vec3(0.0f));

        glBindVertexArray(m_VAO);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_SSBO);

        indirect.Bind();
        pglMultiDrawElementsIndirect(GL_TRIANGLES,
                         GL_UNSIGNED_INT,
                         (const void*)0,
                         (GLsizei)drawCount,
                         0);

        glBindVertexArray(0);
    }

    void WorldRenderer::PrepareVisibleDraws(const glm::mat4& viewProjection,
                                            const glm::vec3& cameraPos,
                                            float cullDistance) {
        if (m_DrawTablesDirty) {
            // No immediate upload needed for base tables because we always upload the filtered visible lists below.
            m_DrawTablesDirty = false;
        }

        const auto planes = ExtractFrustumPlanes(viewProjection);
        const float maxDist = std::max(0.0f, cullDistance);
        const float maxDist2 = maxDist * maxDist;
        const float chunkSizeF = (float)Game::World::CHUNK_SIZE;

        m_VisibleCommands.clear();
        m_VisibleChunkPositions.clear();
        m_VisibleCommands.reserve(m_DrawCommands.size());
        m_VisibleChunkPositions.reserve(m_DrawChunkPositions.size());

        std::size_t visibleIndices = 0;

        for (size_t i = 0; i < m_DrawCommands.size(); ++i) {
            const DrawCommand& cmd = m_DrawCommands[i];
            if (cmd.count == 0 || cmd.instanceCount == 0) continue;

            const glm::vec3 chunkPos = glm::vec3(m_DrawChunkPositions[i]);
            const glm::vec3 center = chunkPos + glm::vec3(chunkSizeF * 0.5f);

            if (maxDist > 0.0f) {
                const glm::vec3 d = center - cameraPos;
                if (glm::dot(d, d) > maxDist2) continue;
            }

            const glm::vec3 aabbMin = chunkPos;
            const glm::vec3 aabbMax = chunkPos + glm::vec3(chunkSizeF);
            if (!AabbIntersectsFrustum(aabbMin, aabbMax, planes)) continue;

            m_VisibleCommands.push_back(cmd);
            m_VisibleChunkPositions.push_back(glm::vec4(chunkPos, 0.0f));
            visibleIndices += (std::size_t)cmd.count;
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_SSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     m_VisibleChunkPositions.size() * sizeof(glm::vec4),
                     m_VisibleChunkPositions.empty() ? nullptr : m_VisibleChunkPositions.data(),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        GetIndirect().Upload(m_VisibleCommands);
        m_LastDrawCount = m_VisibleCommands.size();
        m_LastIndexCount = visibleIndices;
    }

}
