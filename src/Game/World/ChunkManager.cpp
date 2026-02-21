#include "Game/World/ChunkManager.h"

#include "Game/World/Generation.h"
#include "Game/World/SparseVoxelOctree.h"
#include "Engine/Core/QualityManager.h"
#include "Engine/Core/RunLog.h"
#include "Engine/Renderer/Mesh.h"
#include "Engine/Renderer/WorldRenderer.h"
#include "threading/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <system_error>
#include <string>
#include <vector>

namespace Game { namespace World {

    namespace {
        int GetEnvIntClamped(const char* name, int defaultValue, int minValue, int maxValue) {
            const char* v = std::getenv(name);
            if (!v || !*v) return defaultValue;
            char* end = nullptr;
            long parsed = std::strtol(v, &end, 10);
            if (end == v) return defaultValue;
            if (parsed < minValue) return minValue;
            if (parsed > maxValue) return maxValue;
            return (int)parsed;
        }

        float GetEnvFloatClamped(const char* name, float defaultValue, float minValue, float maxValue) {
            const char* v = std::getenv(name);
            if (!v || !*v) return defaultValue;
            char* end = nullptr;
            const float parsed = std::strtof(v, &end);
            if (end == v) return defaultValue;
            return std::clamp(parsed, minValue, maxValue);
        }

        std::string GetEnvString(const char* name, const std::string& defaultValue) {
            const char* v = std::getenv(name);
            if (!v || !*v) return defaultValue;
            return std::string(v);
        }

        bool GetEnvBool(const char* name, bool defaultValue) {
            const char* v = std::getenv(name);
            if (!v || !*v) return defaultValue;
            if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return true;
            if (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F') return false;
            return defaultValue;
        }

        bool HasAnySolidBlock(const std::vector<uint8_t>& blocks) {
            for (uint8_t b : blocks) {
                if (b != 0) return true;
            }
            return false;
        }

        void BuildFallbackMeshFromBlocks(const glm::ivec3& chunkCoord,
                                         const std::vector<uint8_t>& blocks,
                                         std::vector<uint32_t>& outVertices,
                                         std::vector<unsigned int>& outIndices) {
            if (blocks.empty()) return;

            Chunk temp(chunkCoord);
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                for (int y = 0; y < CHUNK_SIZE; ++y) {
                    for (int x = 0; x < CHUNK_SIZE; ++x) {
                        const int idx = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
                        temp.SetBlock(x, y, z, blocks[(std::size_t)idx]);
                    }
                }
            }

            Engine::BuildChunkMeshCPU(temp, outVertices, outIndices);
        }
    }

    ChunkManager::ChunkManager()
        : m_ChunkStoreRoot(GetEnvString("HVE_CHUNK_STORE_ROOT", "minecraft/world_chunks")) {
        const bool svoEnabled = GetEnvIntClamped("HVE_SVO_ENABLE", 1, 0, 1) != 0;
        if (svoEnabled) {
            m_SVO = std::make_unique<SparseVoxelOctree>();
        }
    }
    ChunkManager::~ChunkManager() = default;

    void ChunkManager::Init(Threading::ThreadPool* pool) {
        m_Pool = pool;
    }

    glm::ivec3 ChunkManager::WorldToChunkCoord(const glm::ivec3& world) const {
        auto divFloor = [](int a, int b) {
            // floor division for negatives
            int q = a / b;
            int r = a % b;
            if ((r != 0) && ((r > 0) != (b > 0))) --q;
            return q;
        };

        return glm::ivec3(
            divFloor(world.x, CHUNK_SIZE),
            divFloor(world.y, CHUNK_SIZE),
            divFloor(world.z, CHUNK_SIZE)
        );
    }

    glm::ivec3 ChunkManager::WorldToLocal(const glm::ivec3& world, const glm::ivec3& chunkCoord) const {
        glm::ivec3 base = chunkCoord * CHUNK_SIZE;
        return world - base;
    }

    void ChunkManager::UpdateStreaming(const glm::vec3& playerWorldPos, int viewDistance, int heightChunks) {
        if (!m_Pool) return;

        const int qualityView = std::clamp(Engine::QualityManager::GetViewDistance(), 1, 192);
        static int stickyViewDistance = 0;
        if (stickyViewDistance <= 0) stickyViewDistance = std::max(viewDistance, qualityView);
        const int requested = std::max(viewDistance, qualityView);
        if (requested < stickyViewDistance) {
            stickyViewDistance = std::max(requested, stickyViewDistance - 1);
        } else if (requested > stickyViewDistance) {
            stickyViewDistance = std::min(requested, stickyViewDistance + 2);
        }
        viewDistance = std::clamp(stickyViewDistance, 1, 192);

        if (heightChunks < 1) heightChunks = 1;

        const int defaultInFlight = std::max(12, (int)m_Pool->GetThreadCount() * 6);
        const int maxInFlight = GetEnvIntClamped("HVE_STREAM_INFLIGHT_MAX", defaultInFlight, 1, 512);
        const int defaultEnqueue = std::clamp(10 + viewDistance / 2, 12, 96);
        const int maxEnqueue = GetEnvIntClamped("HVE_STREAM_ENQUEUE_MAX", defaultEnqueue, 1, 192);

        const glm::ivec3 playerBlock((int)std::floor(playerWorldPos.x), (int)std::floor(playerWorldPos.y), (int)std::floor(playerWorldPos.z));
        const glm::ivec3 center = WorldToChunkCoord(playerBlock);

        const int verticalRadius = GetEnvIntClamped("HVE_VERTICAL_RADIUS", 48, 8, 96);
        const int worldMaxYChunk = std::max(0, heightChunks - 1);
        const int minY = std::max(0, center.y - verticalRadius);
        const int maxY = std::min(worldMaxYChunk, center.y + verticalRadius);
        if (minY > maxY) {
            return;
        }

        const bool predictivePrewarm = GetEnvIntClamped("HVE_PREDICTIVE_PREWARM", 1, 0, 1) != 0;
        const int prewarmMaxChunks = GetEnvIntClamped("HVE_PREWARM_CHUNKS", 8, 0, 32);
        const float prewarmSensitivity = GetEnvFloatClamped("HVE_PREWARM_SENSITIVITY", 4.0f, 0.1f, 40.0f);
        const float prewarmSmoothing = GetEnvFloatClamped("HVE_PREWARM_SMOOTHING", 0.22f, 0.01f, 1.0f);

        glm::ivec3 streamCenter = center;
        if (predictivePrewarm && prewarmMaxChunks > 0) {
            if (!m_HasLastStreamPos) {
                m_LastStreamPlayerPos = playerWorldPos;
                m_HasLastStreamPos = true;
            }

            const glm::vec2 deltaXZ(playerWorldPos.x - m_LastStreamPlayerPos.x, playerWorldPos.z - m_LastStreamPlayerPos.z);
            const float dist = glm::length(deltaXZ);
            if (dist > 0.0001f) {
                const glm::vec2 dir = deltaXZ / dist;
                m_StreamDirXZ += (dir - m_StreamDirXZ) * prewarmSmoothing;
                const float dirLen = glm::length(m_StreamDirXZ);
                if (dirLen > 0.0001f) {
                    m_StreamDirXZ /= dirLen;
                }

                const float movementFactor = std::clamp(dist * prewarmSensitivity, 0.0f, 1.0f);
                const int ahead = std::clamp((int)std::round((float)prewarmMaxChunks * movementFactor), 0, prewarmMaxChunks);
                streamCenter.x += (int)std::round(m_StreamDirXZ.x * (float)ahead);
                streamCenter.z += (int)std::round(m_StreamDirXZ.y * (float)ahead);
            }

            m_LastStreamPlayerPos = playerWorldPos;
        }

        std::vector<int> yOrder;
        yOrder.reserve((std::size_t)std::max(16, (maxY - minY + 1) / 2));
        const int clampedCenterY = std::clamp(center.y, minY, maxY);
        auto pushUniqueY = [&](int y) {
            if (y < minY || y > maxY) return;
            if (std::find(yOrder.begin(), yOrder.end(), y) != yOrder.end()) return;
            yOrder.push_back(y);
        };

        pushUniqueY(clampedCenterY);
        for (int d = 1; d <= verticalRadius; ++d) {
            pushUniqueY(clampedCenterY + d);
            pushUniqueY(clampedCenterY - d);
        }

        int enqueued = 0;
        const bool dualQueueStreaming = GetEnvIntClamped("HVE_STREAM_DUAL_QUEUE", 1, 0, 1) != 0;
        const int defaultNearRing = std::clamp(3 + viewDistance / 8, 2, 12);
        const int nearRing = std::clamp(GetEnvIntClamped("HVE_STREAM_NEAR_RING", defaultNearRing, 0, 32), 0, viewDistance);
        const int nearSharePct = GetEnvIntClamped("HVE_STREAM_NEAR_SHARE", 70, 30, 95);

        auto tryEnqueue = [&](const glm::ivec3& coord, int& budgetUsed, int budgetMax) {
            if (enqueued >= maxEnqueue) return;
            if (budgetUsed >= budgetMax) return;
            if ((int)m_InFlightGenerate.load(std::memory_order_relaxed) >= maxInFlight) return;

            ChunkKey key{coord};
            {
                std::shared_lock<std::shared_mutex> lock(m_ChunksMutex);
                if (m_Chunks.find(key) != m_Chunks.end()) return;
            }

            {
                std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
                if (m_Chunks.find(key) != m_Chunks.end()) return;
                auto [newIt, inserted] = m_Chunks.try_emplace(key, coord);
                (void)newIt;
                (void)inserted;
            }

            QueueGenerateAndMesh(coord);
            ++enqueued;
            ++budgetUsed;
        };

        auto enqueueRingRange = [&](int ringStart, int ringEnd, int& budgetUsed, int budgetMax) {
            for (int ring = ringStart; ring <= ringEnd && budgetUsed < budgetMax && enqueued < maxEnqueue && (int)m_InFlightGenerate.load(std::memory_order_relaxed) < maxInFlight; ++ring) {
                for (int yi = 0; yi < (int)yOrder.size() && budgetUsed < budgetMax && enqueued < maxEnqueue && (int)m_InFlightGenerate.load(std::memory_order_relaxed) < maxInFlight; ++yi) {
                const int cy = yOrder[(std::size_t)yi];
                if (ring == 0) {
                        tryEnqueue(glm::ivec3(streamCenter.x, cy, streamCenter.z), budgetUsed, budgetMax);
                    continue;
                }

                const int xMin = streamCenter.x - ring;
                const int xMax = streamCenter.x + ring;
                const int zMin = streamCenter.z - ring;
                const int zMax = streamCenter.z + ring;

                    for (int x = xMin; x <= xMax && budgetUsed < budgetMax && enqueued < maxEnqueue && (int)m_InFlightGenerate.load(std::memory_order_relaxed) < maxInFlight; ++x) {
                        tryEnqueue(glm::ivec3(x, cy, zMin), budgetUsed, budgetMax);
                        tryEnqueue(glm::ivec3(x, cy, zMax), budgetUsed, budgetMax);
                }
                    for (int z = zMin + 1; z <= zMax - 1 && budgetUsed < budgetMax && enqueued < maxEnqueue && (int)m_InFlightGenerate.load(std::memory_order_relaxed) < maxInFlight; ++z) {
                        tryEnqueue(glm::ivec3(xMin, cy, z), budgetUsed, budgetMax);
                        tryEnqueue(glm::ivec3(xMax, cy, z), budgetUsed, budgetMax);
                    }
                }
            }
        };

        if (dualQueueStreaming && viewDistance > 0) {
            const int nearBudget = std::clamp((maxEnqueue * nearSharePct) / 100, 1, maxEnqueue);
            const int farBudget = std::max(0, maxEnqueue - nearBudget);

            int nearUsed = 0;
            enqueueRingRange(0, nearRing, nearUsed, nearBudget);

            if (nearRing < viewDistance && farBudget > 0 && enqueued < maxEnqueue && (int)m_InFlightGenerate.load(std::memory_order_relaxed) < maxInFlight) {
                int farUsed = 0;
                enqueueRingRange(nearRing + 1, viewDistance, farUsed, farBudget);
            }

            if (enqueued < maxEnqueue && (int)m_InFlightGenerate.load(std::memory_order_relaxed) < maxInFlight) {
                int spill = 0;
                enqueueRingRange(0, viewDistance, spill, maxEnqueue - enqueued);
            }
        } else {
            int used = 0;
            enqueueRingRange(0, viewDistance, used, maxEnqueue);
        }
    }

    void ChunkManager::PreloadLargeArea(const glm::vec3& centerPos, int horizontalRadius, int verticalRadius) {
        if (!m_Pool) return;

        horizontalRadius = std::clamp(horizontalRadius, 8, 224);
        verticalRadius = std::clamp(verticalRadius, 4, 96);

        const int defaultInFlight = std::max(16, (int)m_Pool->GetThreadCount() * 10);
        const int maxInFlight = GetEnvIntClamped("HVE_STREAM_INFLIGHT_MAX", defaultInFlight, 1, 4096);
        const int maxBootstrap = GetEnvIntClamped("HVE_PRELOAD_BOOTSTRAP_MAX", 12000, 256, 500000);
        const int farStrideBegin = GetEnvIntClamped("HVE_PRELOAD_FAR_STRIDE_BEGIN", horizontalRadius / 2, 8, 224);
        const int farStride2Begin = GetEnvIntClamped("HVE_PRELOAD_FAR_STRIDE2_BEGIN", (horizontalRadius * 3) / 4, 8, 224);

        const glm::ivec3 center = WorldToChunkCoord(glm::ivec3(
            (int)std::floor(centerPos.x),
            (int)std::floor(centerPos.y),
            (int)std::floor(centerPos.z)));

        std::vector<int> yOrder;
        yOrder.reserve((std::size_t)(verticalRadius * 2 + 1));
        yOrder.push_back(center.y);
        for (int d = 1; d <= verticalRadius; ++d) {
            yOrder.push_back(center.y + d);
            yOrder.push_back(center.y - d);
        }

        int queued = 0;
        auto tryQueue = [&](const glm::ivec3& coord) {
            if (queued >= maxBootstrap) return;
            if ((int)m_InFlightGenerate.load(std::memory_order_relaxed) >= maxInFlight) return;

            ChunkKey key{coord};
            {
                std::shared_lock<std::shared_mutex> readLock(m_ChunksMutex);
                if (m_Chunks.find(key) != m_Chunks.end()) return;
            }

            {
                std::unique_lock<std::shared_mutex> writeLock(m_ChunksMutex);
                if (m_Chunks.find(key) != m_Chunks.end()) return;
                m_Chunks.try_emplace(key, coord);
            }

            QueueGenerateAndMesh(coord);
            ++queued;
        };

        for (int ring = 0; ring <= horizontalRadius && queued < maxBootstrap; ++ring) {
            int yStep = 1;
            if (ring >= farStride2Begin) yStep = 4;
            else if (ring >= farStrideBegin) yStep = 2;

            if (ring == 0) {
                for (int yi = 0; yi < (int)yOrder.size() && queued < maxBootstrap; yi += yStep) {
                    tryQueue(glm::ivec3(center.x, yOrder[(std::size_t)yi], center.z));
                }
                continue;
            }

            const int xMin = center.x - ring;
            const int xMax = center.x + ring;
            const int zMin = center.z - ring;
            const int zMax = center.z + ring;

            for (int yi = 0; yi < (int)yOrder.size() && queued < maxBootstrap; yi += yStep) {
                const int cy = yOrder[(std::size_t)yi];
                for (int x = xMin; x <= xMax && queued < maxBootstrap; ++x) {
                    tryQueue(glm::ivec3(x, cy, zMin));
                    tryQueue(glm::ivec3(x, cy, zMax));
                }
                for (int z = zMin + 1; z <= zMax - 1 && queued < maxBootstrap; ++z) {
                    tryQueue(glm::ivec3(xMin, cy, z));
                    tryQueue(glm::ivec3(xMax, cy, z));
                }
            }
        }

        std::cout << "[PRELOAD] HugeSight queued=" << queued
                  << " center=(" << center.x << "," << center.y << "," << center.z << ")"
                  << " radiusH=" << horizontalRadius
                  << " radiusV=" << verticalRadius
                  << " budget=" << maxBootstrap
                  << std::endl;
    }

    void ChunkManager::UnloadFarChunks(const glm::vec3& playerWorldPos, int viewDistance, int heightChunks, Engine::WorldRenderer& renderer) {
        if (heightChunks < 1) heightChunks = 1;
        const glm::ivec3 playerBlock((int)std::floor(playerWorldPos.x), (int)std::floor(playerWorldPos.y), (int)std::floor(playerWorldPos.z));
        const glm::ivec3 center = WorldToChunkCoord(playerBlock);

        auto inRange = [&](const glm::ivec3& coord) {
            const int dx = std::abs(coord.x - center.x);
            const int dz = std::abs(coord.z - center.z);
            const int dy = std::abs(coord.y - center.y);

            const float HYSTERESIS = 1.35f;
            const int hDist = (int)std::ceil((float)viewDistance * HYSTERESIS);
            return (dx <= hDist) && (dz <= hDist) && (dy <= 48);
        };

        struct DirtySnapshot {
            glm::ivec3 coord{0};
            std::vector<uint8_t> blocks;
        };
        std::vector<DirtySnapshot> dirtyToStore;

        std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
        for (auto it = m_Chunks.begin(); it != m_Chunks.end(); ) {
            const glm::ivec3 coord = it->first.coord;
            if (inRange(coord) || it->second.meshingQueued) {
                ++it;
                continue;
            }

            if (it->second.chunk.IsDirty()) {
                DirtySnapshot snap;
                snap.coord = coord;
                it->second.chunk.CopyBlocksTo(snap.blocks);
                it->second.chunk.SetDirty(false);
                dirtyToStore.emplace_back(std::move(snap));
            }

            renderer.RemoveChunk(coord);
            if (m_SVO) m_SVO->MarkChunkRemoved(coord);
            it = m_Chunks.erase(it);
        }

        lock.unlock();
        for (const auto& s : dirtyToStore) {
            SaveChunkToStore(s.coord, s.blocks);
        }
    }

    void ChunkManager::QueueGenerateAndMesh(const glm::ivec3& chunkCoord) {
        if (!m_Pool) return;

        {
            std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
            ChunkKey key{chunkCoord};
            auto it = m_Chunks.find(key);
            if (it == m_Chunks.end()) return;
            if (it->second.meshingQueued) return;
            it->second.meshingQueued = true;
            m_InFlightGenerate.fetch_add(1, std::memory_order_relaxed);
        }

        m_Pool->Enqueue([this, chunkCoord]() {
            ChunkMeshCPU done;
            done.key = ChunkKey{chunkCoord};
            done.chunkCoord = chunkCoord;
            done.hasBlocks = true;

            // Deterministic padded volume so chunk boundaries don't "morph" when neighbors load/unload.
            constexpr int PAD = CHUNK_SIZE + 2;
            std::vector<uint8_t> padded;
            padded.resize((std::size_t)PAD * (std::size_t)PAD * (std::size_t)PAD);

            const glm::ivec3 baseWorld = chunkCoord * CHUNK_SIZE;
            for (int pz = 0; pz < PAD; ++pz) {
                for (int py = 0; py < PAD; ++py) {
                    for (int px = 0; px < PAD; ++px) {
                        const glm::ivec3 world = baseWorld + glm::ivec3(px - 1, py - 1, pz - 1);
                        const int idx = px + py * PAD + pz * PAD * PAD;
                        padded[(std::size_t)idx] = Generation::GetBlockAtWorld(world.x, world.y, world.z);
                    }
                }
            }

            // Store chunk blocks (interior only) for persistence.
            done.blocks.resize((std::size_t)CHUNK_SIZE * (std::size_t)CHUNK_SIZE * (std::size_t)CHUNK_SIZE);
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                for (int y = 0; y < CHUNK_SIZE; ++y) {
                    for (int x = 0; x < CHUNK_SIZE; ++x) {
                        const int idx = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
                        const int pidx = (x + 1) + (y + 1) * PAD + (z + 1) * PAD * PAD;
                        done.blocks[(std::size_t)idx] = padded[(std::size_t)pidx];
                    }
                }
            }

            std::vector<uint8_t> persisted;
            if (TryLoadChunkFromStore(chunkCoord, persisted) && persisted.size() == done.blocks.size()) {
                done.blocks = std::move(persisted);

                // Refresh interior of the padded volume so mesh reflects persisted edits.
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    for (int y = 0; y < CHUNK_SIZE; ++y) {
                        for (int x = 0; x < CHUNK_SIZE; ++x) {
                            const int idx = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
                            const int pidx = (x + 1) + (y + 1) * PAD + (z + 1) * PAD * PAD;
                            padded[(std::size_t)pidx] = done.blocks[(std::size_t)idx];
                        }
                    }
                }
            }

            Engine::BuildChunkMeshCPU_Padded(padded.data(), PAD, done.vertices, done.indices);
            if (done.vertices.empty() && done.indices.empty() && HasAnySolidBlock(done.blocks)) {
                BuildFallbackMeshFromBlocks(done.chunkCoord, done.blocks, done.vertices, done.indices);
            }

            {
                std::lock_guard<std::mutex> lock(m_CompletedMutex);
                m_Completed.emplace_back(std::move(done));
            }
        });
    }

    void ChunkManager::QueueRemeshCopy(const glm::ivec3& chunkCoord) {
        if (!m_Pool) return;

        {
            std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
            ChunkKey key{chunkCoord};
            auto it = m_Chunks.find(key);
            if (it == m_Chunks.end()) return;
            if (it->second.meshingQueued) return;
            it->second.meshingQueued = true;
        }

        EnqueueRemeshJob(chunkCoord);
    }

    void ChunkManager::EnqueueRemeshJob(const glm::ivec3& chunkCoord) {
        if (!m_Pool) return;
        m_InFlightRemesh.fetch_add(1, std::memory_order_relaxed);
        m_Pool->EnqueueUrgent([this, chunkCoord]() {
            ChunkMeshCPU done;
            done.key = ChunkKey{chunkCoord};
            done.chunkCoord = chunkCoord;
            done.hasBlocks = false;

            constexpr int PAD = CHUNK_SIZE + 2;
            std::vector<uint8_t> padded;
            CopyPaddedForRemesh(chunkCoord, padded);

            Engine::BuildChunkMeshCPU_Padded(padded.data(), PAD, done.vertices, done.indices);
            if (done.vertices.empty() && done.indices.empty()) {
                done.blocks.resize((std::size_t)CHUNK_SIZE * (std::size_t)CHUNK_SIZE * (std::size_t)CHUNK_SIZE);
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    for (int y = 0; y < CHUNK_SIZE; ++y) {
                        for (int x = 0; x < CHUNK_SIZE; ++x) {
                            const int idx = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
                            const int pidx = (x + 1) + (y + 1) * PAD + (z + 1) * PAD * PAD;
                            done.blocks[(std::size_t)idx] = padded[(std::size_t)pidx];
                        }
                    }
                }
                if (HasAnySolidBlock(done.blocks)) {
                    BuildFallbackMeshFromBlocks(done.chunkCoord, done.blocks, done.vertices, done.indices);
                }
                done.blocks.clear();
            }

            {
                std::lock_guard<std::mutex> lock(m_CompletedMutex);
                m_Completed.emplace_back(std::move(done));
            }
        });
    }

    void ChunkManager::RequestRemeshLocked(const glm::ivec3& chunkCoord) {
        ChunkKey key{chunkCoord};
        auto it = m_Chunks.find(key);
        if (it == m_Chunks.end()) return;
        if (it->second.meshingQueued) {
            it->second.needsRemesh = true;
            return;
        }
        const int maxInFlight = GetEnvIntClamped("HVE_REMESH_INFLIGHT_MAX", 32, 1, 512);
        const int maxDeferred = GetEnvIntClamped("HVE_REMESH_DEFERRED_MAX", 512, 0, 4096);
        if ((int)m_InFlightRemesh.load(std::memory_order_relaxed) >= maxInFlight) {
            it->second.needsRemesh = true;
            if (!it->second.remeshDeferred && (int)m_RemeshDeferred.size() < maxDeferred) {
                it->second.remeshDeferred = true;
                m_RemeshDeferred.push_back(chunkCoord);
            }
            return;
        }
        it->second.meshingQueued = true;
        EnqueueRemeshJob(chunkCoord);
    }

    void ChunkManager::QueueRemeshNeighborhood27Locked(const glm::ivec3& centerCoord) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const glm::ivec3 n = centerCoord + glm::ivec3(dx, dy, dz);
                    ChunkKey nk{n};
                    if (m_Chunks.find(nk) == m_Chunks.end()) continue;

                    if (m_BulkEditDepth > 0) {
                        m_BulkDirtyChunks.insert(nk);
                    } else {
                        RequestRemeshLocked(n);
                    }
                }
            }
        }
    }

    void ChunkManager::CopyPaddedForRemesh(const glm::ivec3& chunkCoord, std::vector<uint8_t>& out) const {
        constexpr int PAD = CHUNK_SIZE + 2;
        out.resize((std::size_t)PAD * (std::size_t)PAD * (std::size_t)PAD);

        const glm::ivec3 baseWorld = chunkCoord * CHUNK_SIZE;
        std::shared_lock<std::shared_mutex> lock(m_ChunksMutex);
        for (int pz = 0; pz < PAD; ++pz) {
            for (int py = 0; py < PAD; ++py) {
                for (int px = 0; px < PAD; ++px) {
                    const glm::ivec3 world = baseWorld + glm::ivec3(px - 1, py - 1, pz - 1);
                    const int idx = px + py * PAD + pz * PAD * PAD;
                    const glm::ivec3 cc = WorldToChunkCoord(world);
                    ChunkKey key{cc};
                    auto it = m_Chunks.find(key);
                    if (it == m_Chunks.end()) {
                        out[(std::size_t)idx] = 0;
                        continue;
                    }
                    const glm::ivec3 local = WorldToLocal(world, cc);
                    out[(std::size_t)idx] = it->second.chunk.GetBlock(local.x, local.y, local.z);
                }
            }
        }
    }

    void ChunkManager::PumpCompleted(Engine::WorldRenderer& renderer, int maxUploads) {
        const int hardCap = GetEnvIntClamped("HVE_UPLOAD_HARD_CAP", 64, 8, 512);
        if (maxUploads < 1) maxUploads = hardCap;
        if (maxUploads > hardCap) maxUploads = hardCap;

        std::vector<ChunkMeshCPU> completed;
        {
            std::lock_guard<std::mutex> lock(m_CompletedMutex);
            if (m_Completed.empty()) return;

            // If we have a limit, take only up to that many
            if (maxUploads > 0 && m_Completed.size() > (size_t)maxUploads) {
                // Move first 'maxUploads' elements to 'completed'
                auto begin = m_Completed.begin();
                auto split = begin + maxUploads;
                completed.insert(completed.end(), std::make_move_iterator(begin), std::make_move_iterator(split));
                m_Completed.erase(begin, split);
            } else {
                // Take all
                completed.swap(m_Completed);
            }
        }

        for (auto& job : completed) {
            if (!job.hasBlocks) {
                const std::size_t cur = m_InFlightRemesh.load(std::memory_order_relaxed);
                if (cur > 0) {
                    m_InFlightRemesh.fetch_sub(1, std::memory_order_relaxed);
                }
            }
            if (job.hasBlocks) {
                const std::size_t cur = m_InFlightGenerate.load(std::memory_order_relaxed);
                if (cur > 0) {
                    m_InFlightGenerate.fetch_sub(1, std::memory_order_relaxed);
                }
            }

            {
                std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
                ChunkKey key{job.chunkCoord};
                auto it = m_Chunks.find(key);
                if (it == m_Chunks.end()) continue;

                it->second.meshingQueued = false;

                if (job.hasBlocks && !job.blocks.empty()) {
                    for (int x = 0; x < CHUNK_SIZE; ++x) {
                        for (int y = 0; y < CHUNK_SIZE; ++y) {
                            for (int z = 0; z < CHUNK_SIZE; ++z) {
                                const int idx = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
                                it->second.chunk.SetBlock(x, y, z, job.blocks[(std::size_t)idx]);
                            }
                        }
                    }
                    it->second.ready = true;
                    it->second.chunk.SetDirty(false);
                    if (m_SVO) m_SVO->MarkChunkResident(job.chunkCoord);
                }
            }

            // Upload mesh without holding chunk map lock.
            renderer.UploadOrUpdateChunk(job.chunkCoord, job.vertices, job.indices);

            std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
            ChunkKey key{job.chunkCoord};
            auto it = m_Chunks.find(key);
            if (it == m_Chunks.end()) continue;

            it->second.lastMeshStamp = ++m_MeshStampCounter;

            if (it->second.needsRemesh) {
                const int maxInFlight = GetEnvIntClamped("HVE_REMESH_INFLIGHT_MAX", 32, 1, 512);
                const int maxDeferred = GetEnvIntClamped("HVE_REMESH_DEFERRED_MAX", 512, 0, 4096);
                if ((int)m_InFlightRemesh.load(std::memory_order_relaxed) >= maxInFlight) {
                    if (!it->second.remeshDeferred && (int)m_RemeshDeferred.size() < maxDeferred) {
                        it->second.remeshDeferred = true;
                        m_RemeshDeferred.push_back(job.chunkCoord);
                    }
                } else {
                    it->second.needsRemesh = false;
                    it->second.remeshDeferred = false;
                    it->second.meshingQueued = true;
                    lock.unlock();
                    EnqueueRemeshJob(job.chunkCoord);
                    continue;
                }
            }
        }

        const int maxDeferredPerFrame = GetEnvIntClamped("HVE_REMESH_DEFERRED_PER_FRAME", 4, 0, 64);
        if (maxDeferredPerFrame > 0) {
            const int maxInFlight = GetEnvIntClamped("HVE_REMESH_INFLIGHT_MAX", 32, 1, 512);
            int processed = 0;
            std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
            while (!m_RemeshDeferred.empty() && processed < maxDeferredPerFrame) {
                if ((int)m_InFlightRemesh.load(std::memory_order_relaxed) >= maxInFlight) break;
                const glm::ivec3 coord = m_RemeshDeferred.front();
                m_RemeshDeferred.pop_front();

                ChunkKey key{coord};
                auto it = m_Chunks.find(key);
                if (it == m_Chunks.end()) continue;
                if (it->second.meshingQueued) {
                    it->second.needsRemesh = true;
                    continue;
                }
                it->second.needsRemesh = false;
                it->second.remeshDeferred = false;
                it->second.meshingQueued = true;
                lock.unlock();
                EnqueueRemeshJob(coord);
                lock.lock();
                processed++;
            }
        }
    }

    uint8_t ChunkManager::GetBlockWorld(const glm::ivec3& world) const {
        std::shared_lock<std::shared_mutex> lock(m_ChunksMutex);
        glm::ivec3 cc = WorldToChunkCoord(world);
        ChunkKey key{cc};
        auto it = m_Chunks.find(key);
        if (it == m_Chunks.end()) return 0;
        glm::ivec3 local = WorldToLocal(world, cc);
        return it->second.chunk.GetBlock(local.x, local.y, local.z);
    }

    void ChunkManager::SetBlockWorld(const glm::ivec3& world, uint8_t id) {
        std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
        glm::ivec3 cc = WorldToChunkCoord(world);
        ChunkKey key{cc};
        auto it = m_Chunks.find(key);
        if (it == m_Chunks.end()) return;

        glm::ivec3 local = WorldToLocal(world, cc);
        it->second.chunk.SetBlock(local.x, local.y, local.z, id);
        QueueRemeshNeighborhood27Locked(cc);
    }

    void ChunkManager::BeginBulkEdit() {
        std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
        ++m_BulkEditDepth;
    }

    void ChunkManager::EndBulkEdit() {
        std::vector<glm::ivec3> remeshTargets;
        {
            std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
            if (m_BulkEditDepth <= 0) {
                m_BulkEditDepth = 0;
                return;
            }

            --m_BulkEditDepth;
            if (m_BulkEditDepth > 0) {
                return;
            }

            remeshTargets.reserve(m_BulkDirtyChunks.size());
            for (const auto& key : m_BulkDirtyChunks) {
                remeshTargets.push_back(key.coord);
            }
            m_BulkDirtyChunks.clear();
        }

        if (remeshTargets.empty()) return;

        std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
        for (const auto& coord : remeshTargets) {
            RequestRemeshLocked(coord);
        }
    }

    void ChunkManager::RequestRemesh(const glm::ivec3& chunkCoord) {
        std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
        RequestRemeshLocked(chunkCoord);
    }

    void ChunkManager::ClearAll(Engine::WorldRenderer& renderer) {
        {
            std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
            for (auto& kv : m_Chunks) {
                renderer.RemoveChunk(kv.first.coord);
            }
            m_Chunks.clear();
            if (m_SVO) m_SVO->Clear();
            m_InFlightGenerate = 0;
            m_InFlightRemesh.store(0, std::memory_order_relaxed);
            m_MeshStampCounter = 0;
            m_RemeshDeferred.clear();
        }

        // Drop any completed worker jobs too.
        {
            std::lock_guard<std::mutex> lock(m_CompletedMutex);
            m_Completed.clear();
        }
    }

    namespace {
        struct WorldFileHeader {
            uint32_t magic = 0;
            uint32_t version = 0;
            int32_t seed = 0;
            uint32_t chunkCount = 0;
        };

        static constexpr uint32_t kMagic = 0x57455648u; // 'HVEW'
        static constexpr uint32_t kVersion = 2;
        static constexpr uint32_t kMinSupportedVersion = 1;
        static constexpr uint32_t kChunkBlobMagic = 0x4B484356u; // 'VCHK'
        static constexpr uint32_t kChunkBlobVersion = 1;

        static uint32_t ComputeChunkChecksum(const uint8_t* data, std::size_t len) {
            uint32_t h = 2166136261u;
            for (std::size_t i = 0; i < len; ++i) {
                h ^= (uint32_t)data[i];
                h *= 16777619u;
            }
            return h;
        }

        static void RLE_Encode(const uint8_t* data, std::size_t len, std::vector<uint8_t>& out) {
            out.clear();
            out.reserve(len / 2);

            std::size_t i = 0;
            while (i < len) {
                const uint8_t value = data[i];
                uint16_t run = 1;
                while ((i + run) < len && data[i + run] == value && run < 65535) {
                    run++;
                }

                out.push_back(value);
                out.push_back((uint8_t)(run & 0xFF));
                out.push_back((uint8_t)((run >> 8) & 0xFF));
                i += (std::size_t)run;
            }
        }

        static bool RLE_Decode(const uint8_t* data, std::size_t len, std::vector<uint8_t>& out, std::size_t expectedLen) {
            out.clear();
            out.reserve(expectedLen);

            std::size_t i = 0;
            while (i + 2 < len) {
                const uint8_t value = data[i + 0];
                const uint16_t run = (uint16_t)data[i + 1] | ((uint16_t)data[i + 2] << 8);
                i += 3;
                if (run == 0) return false;
                if (out.size() + (std::size_t)run > expectedLen) return false;
                out.insert(out.end(), (std::size_t)run, value);
            }

            return out.size() == expectedLen;
        }

        struct DecodedChunkData {
            glm::ivec3 coord{0};
            std::vector<uint8_t> blocks;
        };

        static int GetBackupCount() {
            return GetEnvIntClamped("HVE_SAVE_BACKUPS", 3, 0, 10);
        }

        static std::string BackupPathFor(const std::string& basePath, int index) {
            return basePath + ".bak" + std::to_string(index);
        }

        static bool MoveOrReplaceFile(const std::string& src, const std::string& dst) {
            std::error_code ec;
            std::filesystem::remove(dst, ec);
            ec.clear();
            std::filesystem::rename(src, dst, ec);
            if (!ec) return true;

            ec.clear();
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return false;

            ec.clear();
            std::filesystem::remove(src, ec);
            return true;
        }

        static bool ReadWorldHeader(const std::string& filePath, WorldFileHeader& outHdr, std::string& outError) {
            std::ifstream f(filePath, std::ios::binary);
            if (!f.is_open()) {
                outError = "Open failed";
                return false;
            }

            f.read((char*)&outHdr, sizeof(outHdr));
            if (!f.good()) {
                outError = "Header read failed";
                return false;
            }
            if (outHdr.magic != kMagic) {
                outError = "Header magic invalid";
                return false;
            }
            if (outHdr.version < kMinSupportedVersion || outHdr.version > kVersion) {
                outError = "Header version invalid";
                return false;
            }

            return true;
        }

        static bool DecodeWorldFile(const std::string& filePath,
                                    int32_t& outSeed,
                                    std::vector<DecodedChunkData>& outChunks,
                                    std::string& outError,
                                    uint32_t* outChunkCount = nullptr) {
            outChunks.clear();

            WorldFileHeader hdr;
            if (!ReadWorldHeader(filePath, hdr, outError)) return false;

            std::ifstream f(filePath, std::ios::binary);
            if (!f.is_open()) {
                outError = "Open failed";
                return false;
            }
            f.seekg((std::streamoff)sizeof(WorldFileHeader), std::ios::beg);

            std::vector<uint8_t> rle;
            std::vector<uint8_t> blocks;
            outChunks.reserve(hdr.chunkCount);

            for (uint32_t i = 0; i < hdr.chunkCount; ++i) {
                int32_t cx = 0, cy = 0, cz = 0;
                uint32_t rleSize = 0;
                f.read((char*)&cx, sizeof(int32_t));
                f.read((char*)&cy, sizeof(int32_t));
                f.read((char*)&cz, sizeof(int32_t));
                f.read((char*)&rleSize, sizeof(uint32_t));
                if (!f.good()) {
                    outError = "Read failed";
                    return false;
                }

                rle.resize(rleSize);
                if (rleSize > 0) {
                    f.read((char*)rle.data(), (std::streamsize)rleSize);
                    if (!f.good()) {
                        outError = "Read failed";
                        return false;
                    }
                }

                uint32_t storedChecksum = 0;
                if (hdr.version >= 2) {
                    f.read((char*)&storedChecksum, sizeof(uint32_t));
                    if (!f.good()) {
                        outError = "Checksum read failed";
                        return false;
                    }
                }

                if (!RLE_Decode(rle.data(), rle.size(), blocks, Chunk::BlockCount)) {
                    outError = "Decode failed";
                    return false;
                }

                if (hdr.version >= 2) {
                    const uint32_t computed = ComputeChunkChecksum(blocks.data(), blocks.size());
                    if (computed != storedChecksum) {
                        outError = "Checksum failed";
                        return false;
                    }
                }

                DecodedChunkData dc;
                dc.coord = glm::ivec3(cx, cy, cz);
                dc.blocks = blocks;
                outChunks.emplace_back(std::move(dc));
            }

            outSeed = hdr.seed;
            if (outChunkCount) *outChunkCount = hdr.chunkCount;
            return true;
        }

        static bool WriteWorldFile(const std::string& filePath,
                                   int32_t seed,
                                   const std::vector<std::pair<glm::ivec3, std::vector<uint8_t>>>& chunks,
                                   std::string& outError) {
            std::ofstream f(filePath, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) {
                outError = "Open failed";
                return false;
            }

            WorldFileHeader hdr;
            hdr.magic = kMagic;
            hdr.version = kVersion;
            hdr.seed = seed;
            hdr.chunkCount = (uint32_t)chunks.size();

            f.write((const char*)&hdr, sizeof(hdr));
            if (!f.good()) {
                outError = "Header write failed";
                return false;
            }

            std::vector<uint8_t> rle;
            for (const auto& c : chunks) {
                const glm::ivec3 coord = c.first;
                const auto& blocks = c.second;

                RLE_Encode(blocks.data(), blocks.size(), rle);
                const uint32_t rleSize = (uint32_t)rle.size();

                f.write((const char*)&coord.x, sizeof(int32_t));
                f.write((const char*)&coord.y, sizeof(int32_t));
                f.write((const char*)&coord.z, sizeof(int32_t));
                f.write((const char*)&rleSize, sizeof(uint32_t));
                if (rleSize > 0) {
                    f.write((const char*)rle.data(), (std::streamsize)rleSize);
                }
                const uint32_t checksum = ComputeChunkChecksum(blocks.data(), blocks.size());
                f.write((const char*)&checksum, sizeof(uint32_t));
                if (!f.good()) {
                    outError = "Chunk write failed";
                    return false;
                }
            }

            f.flush();
            if (!f.good()) {
                outError = "Flush failed";
                return false;
            }
            return true;
        }

        static bool TryResolveLoadPath(const std::string& primaryPath,
                                       std::string& outResolvedPath,
                                       WorldFileHeader& outHeader,
                                       std::string& outError) {
            const int backupCount = GetBackupCount();
            std::vector<std::string> candidates;
            candidates.reserve((std::size_t)backupCount + 1);
            candidates.push_back(primaryPath);
            for (int i = 1; i <= backupCount; ++i) {
                candidates.push_back(BackupPathFor(primaryPath, i));
            }

            std::error_code ec;
            std::string firstError;
            for (const std::string& p : candidates) {
                if (!std::filesystem::exists(p, ec)) {
                    ec.clear();
                    continue;
                }

                std::string readErr;
                WorldFileHeader hdr;
                if (ReadWorldHeader(p, hdr, readErr)) {
                    outResolvedPath = p;
                    outHeader = hdr;
                    outError.clear();
                    return true;
                }
                if (firstError.empty()) firstError = readErr;
            }

            outError = firstError.empty() ? "No readable world file found" : firstError;
            return false;
        }
    }

    bool ChunkManager::SaveWorldToFile(const char* path) const {
        if (!path || !*path) return false;
        const bool traceIo = GetEnvBool("HVE_TRACE_IO", true);

        const_cast<ChunkManager*>(this)->SetChunkStoreRootFromWorldPath(path);

        // Only save chunks that have finished generation at least once.
        std::vector<std::pair<glm::ivec3, std::vector<uint8_t>>> chunks;
        {
            std::shared_lock<std::shared_mutex> lock(m_ChunksMutex);
            chunks.reserve(m_Chunks.size());
            for (const auto& kv : m_Chunks) {
                if (!kv.second.ready) continue;
                std::vector<uint8_t> blocks;
                kv.second.chunk.CopyBlocksTo(blocks);
                chunks.emplace_back(kv.first.coord, std::move(blocks));
            }
        }

        if (traceIo) {
            Engine::RunLog::Info(std::string("IO SaveWorld start path=") + path + " chunks=" + std::to_string(chunks.size()));
        }

        const std::string targetPath(path);
        const std::string tempPath = targetPath + ".tmp";

        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(targetPath).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }

        std::string writeErr;
        if (!WriteWorldFile(tempPath, Generation::GetSeed(), chunks, writeErr)) {
            if (traceIo) {
                Engine::RunLog::Error(std::string("IO SaveWorld write failed path=") + tempPath + " err=" + writeErr);
            }
            std::filesystem::remove(tempPath, ec);
            return false;
        }

        const int backupCount = GetBackupCount();
        if (backupCount > 0 && std::filesystem::exists(targetPath, ec)) {
            for (int i = backupCount; i >= 1; --i) {
                const std::string src = (i == 1) ? targetPath : BackupPathFor(targetPath, i - 1);
                const std::string dst = BackupPathFor(targetPath, i);
                if (!std::filesystem::exists(src, ec)) {
                    ec.clear();
                    continue;
                }
                if (!MoveOrReplaceFile(src, dst)) {
                    std::filesystem::remove(tempPath, ec);
                    return false;
                }
            }
        }

        if (!MoveOrReplaceFile(tempPath, targetPath)) {
            if (traceIo) {
                Engine::RunLog::Error(std::string("IO SaveWorld move failed temp=") + tempPath + " target=" + targetPath);
            }
            std::filesystem::remove(tempPath, ec);
            return false;
        }

        const bool ok = std::filesystem::exists(targetPath, ec);
        if (traceIo) {
            Engine::RunLog::Info(std::string("IO SaveWorld done path=") + targetPath + " ok=" + (ok ? "1" : "0"));
        }
        return ok;
    }

    bool ChunkManager::LoadWorldFromFile(const char* path, Engine::WorldRenderer& renderer) {
        if (!path || !*path) return false;
        const bool traceIo = GetEnvBool("HVE_TRACE_IO", true);

        if (traceIo) {
            Engine::RunLog::Info(std::string("IO LoadWorld start path=") + path);
        }

        SetChunkStoreRootFromWorldPath(path);

        std::string resolvedPath;
        WorldFileHeader hdr;
        std::string resolveError;
        if (!TryResolveLoadPath(path, resolvedPath, hdr, resolveError)) {
            if (traceIo) {
                Engine::RunLog::Error(std::string("IO LoadWorld resolve failed path=") + path + " err=" + resolveError);
            }
            return false;
        }

        int32_t seed = 0;
        std::vector<DecodedChunkData> decoded;
        std::string decodeErr;
        if (!DecodeWorldFile(resolvedPath, seed, decoded, decodeErr, nullptr)) {
            if (traceIo) {
                Engine::RunLog::Error(std::string("IO LoadWorld decode failed path=") + resolvedPath + " err=" + decodeErr);
            }
            return false;
        }

        // Reset world state.
        ClearAll(renderer);
        Generation::Init((int)seed);

        for (const auto& dc : decoded) {
            const glm::ivec3 coord = dc.coord;
            ChunkKey key{coord};
            auto [it, inserted] = m_Chunks.try_emplace(key, coord);
            (void)inserted;

            it->second.chunk.CopyBlocksFrom(dc.blocks.data(), dc.blocks.size());
            it->second.chunk.SetDirty(false);
            it->second.ready = true;
            it->second.meshingQueued = false;
            if (m_SVO) m_SVO->MarkChunkResident(coord);

            // Build mesh asynchronously from loaded blocks.
            RequestRemesh(coord);
        }

        if (traceIo) {
            Engine::RunLog::Info(std::string("IO LoadWorld done path=") + resolvedPath + " chunks=" + std::to_string(decoded.size()));
        }
        return true;
    }

    bool ChunkManager::BeginLoadWorldFromFileAsync(const char* path, Engine::WorldRenderer& renderer) {
        if (!m_Pool) return false;
        if (!path || !*path) return false;
        const bool traceIo = GetEnvBool("HVE_TRACE_IO", true);

        if (traceIo) {
            Engine::RunLog::Info(std::string("IO AsyncLoad start path=") + path);
        }

        SetChunkStoreRootFromWorldPath(path);

        {
            std::lock_guard<std::mutex> lock(m_LoadMutex);
            if (m_LoadActive) return false;
        }

        std::string resolvedPath;
        WorldFileHeader hdr;
        std::string resolveError;
        if (!TryResolveLoadPath(path, resolvedPath, hdr, resolveError)) {
            if (traceIo) {
                Engine::RunLog::Error(std::string("IO AsyncLoad resolve failed path=") + path + " err=" + resolveError);
            }
            return false;
        }

        // Reset world state now (main thread).
        ClearAll(renderer);
        Generation::Init((int)hdr.seed);

        {
            std::lock_guard<std::mutex> lock(m_LoadMutex);
            m_LoadQueue.clear();
            m_LoadActive = true;
            m_LoadReaderDone = false;
            m_LoadFailed = false;
            m_LoadStatus.clear();
            m_LoadTotal = hdr.chunkCount;
            m_LoadRead = 0;
        }

        const std::string pathCopy(resolvedPath);
        m_Pool->Enqueue([this, pathCopy]() {
            WorldFileHeader hdr2;
            std::ifstream f(pathCopy.c_str(), std::ios::binary);
            if (!f.is_open()) {
                std::lock_guard<std::mutex> lock(m_LoadMutex);
                m_LoadFailed = true;
                m_LoadStatus = "Open failed";
                m_LoadReaderDone = true;
                return;
            }

            f.read((char*)&hdr2, sizeof(hdr2));
            if (!f.good() || hdr2.magic != kMagic || hdr2.version < kMinSupportedVersion || hdr2.version > kVersion) {
                std::lock_guard<std::mutex> lock(m_LoadMutex);
                m_LoadFailed = true;
                m_LoadStatus = "Header invalid";
                m_LoadReaderDone = true;
                return;
            }

            std::vector<uint8_t> rle;
            std::vector<uint8_t> blocks;
            rle.reserve(64);

            for (uint32_t i = 0; i < hdr2.chunkCount; ++i) {
                int32_t cx = 0, cy = 0, cz = 0;
                uint32_t rleSize = 0;
                f.read((char*)&cx, sizeof(int32_t));
                f.read((char*)&cy, sizeof(int32_t));
                f.read((char*)&cz, sizeof(int32_t));
                f.read((char*)&rleSize, sizeof(uint32_t));
                if (!f.good()) {
                    std::lock_guard<std::mutex> lock(m_LoadMutex);
                    m_LoadFailed = true;
                    m_LoadStatus = "Read failed";
                    m_LoadReaderDone = true;
                    return;
                }

                rle.resize(rleSize);
                if (rleSize > 0) {
                    f.read((char*)rle.data(), (std::streamsize)rleSize);
                    if (!f.good()) {
                        std::lock_guard<std::mutex> lock(m_LoadMutex);
                        m_LoadFailed = true;
                        m_LoadStatus = "Read failed";
                        m_LoadReaderDone = true;
                        return;
                    }
                }

                uint32_t storedChecksum = 0;
                if (hdr2.version >= 2) {
                    f.read((char*)&storedChecksum, sizeof(uint32_t));
                    if (!f.good()) {
                        std::lock_guard<std::mutex> lock(m_LoadMutex);
                        m_LoadFailed = true;
                        m_LoadStatus = "Checksum read failed";
                        m_LoadReaderDone = true;
                        return;
                    }
                }

                if (!RLE_Decode(rle.data(), rle.size(), blocks, Chunk::BlockCount)) {
                    std::lock_guard<std::mutex> lock(m_LoadMutex);
                    m_LoadFailed = true;
                    m_LoadStatus = "Decode failed";
                    m_LoadReaderDone = true;
                    return;
                }

                if (hdr2.version >= 2) {
                    const uint32_t computed = ComputeChunkChecksum(blocks.data(), blocks.size());
                    if (computed != storedChecksum) {
                        std::lock_guard<std::mutex> lock(m_LoadMutex);
                        m_LoadFailed = true;
                        m_LoadStatus = "Checksum failed";
                        m_LoadReaderDone = true;
                        return;
                    }
                }

                LoadedChunk lc;
                lc.coord = glm::ivec3(cx, cy, cz);
                lc.blocks = blocks; // copy; blocks buffer reused by decoder

                {
                    std::lock_guard<std::mutex> lock(m_LoadMutex);
                    m_LoadQueue.emplace_back(std::move(lc));
                    m_LoadRead += 1;
                }
            }

            std::lock_guard<std::mutex> lock(m_LoadMutex);
            m_LoadReaderDone = true;
        });

        return true;
    }

    void ChunkManager::PumpAsyncLoad(Engine::WorldRenderer& renderer, int maxChunksPerFrame) {
        (void)renderer;
        if (maxChunksPerFrame < 1) maxChunksPerFrame = 1;

        using namespace std::chrono;
        auto startTime = high_resolution_clock::now();
        // 4ms budget for main thread chunk integration to ensure >60fps (16ms frame time)
        // This prevents input lag when streaming large worlds.
        // Increased to 8ms to speed up loading at the cost of slight frame drops during heavy travel.
        constexpr auto kTimeBudget = milliseconds(8);

        int imported = 0;
        while (imported < maxChunksPerFrame) {
            LoadedChunk lc;
            {
                std::lock_guard<std::mutex> lock(m_LoadMutex);
                if (m_LoadQueue.empty()) break;
                lc = std::move(m_LoadQueue.front());
                m_LoadQueue.pop_front();
            }

            const glm::ivec3 coord = lc.coord;
            ChunkKey key{coord};
            {
                std::unique_lock<std::shared_mutex> lock(m_ChunksMutex);
                auto [it, inserted] = m_Chunks.try_emplace(key, coord);
                (void)inserted;
                it->second.chunk.CopyBlocksFrom(lc.blocks.data(), lc.blocks.size());
                it->second.chunk.SetDirty(false);
                it->second.ready = true;
                it->second.meshingQueued = false;
                it->second.needsRemesh = false;
                if (m_SVO) m_SVO->MarkChunkResident(coord);

                QueueRemeshNeighborhood27Locked(coord);
            }

            imported += 1;

            // Check time budget
            auto now = high_resolution_clock::now();
            if (duration_cast<milliseconds>(now - startTime) >= kTimeBudget) {
                break;
            }
        }

        // If the reader is done and we've drained the queue, mark the load complete.
        {
            std::lock_guard<std::mutex> lock(m_LoadMutex);
            if (m_LoadActive && m_LoadReaderDone && m_LoadQueue.empty()) {
                if (m_LoadFailed) {
                    if (m_LoadStatus.empty()) m_LoadStatus = "Failed";
                } else {
                    m_LoadStatus = "OK";
                }
                m_LoadActive = false;
            }
        }
    }

    bool ChunkManager::IsAsyncLoadInProgress() const {
        std::lock_guard<std::mutex> lock(m_LoadMutex);
        return m_LoadActive;
    }

    bool ChunkManager::IsAsyncLoadFinished() const {
        std::lock_guard<std::mutex> lock(m_LoadMutex);
        return (!m_LoadActive) && m_LoadReaderDone;
    }

    float ChunkManager::GetAsyncLoadProgress01() const {
        std::lock_guard<std::mutex> lock(m_LoadMutex);
        if (m_LoadTotal == 0) return m_LoadReaderDone ? 1.0f : 0.0f;
        const float p = (float)m_LoadRead / (float)m_LoadTotal;
        return std::clamp(p, 0.0f, 1.0f);
    }

    std::string ChunkManager::GetAsyncLoadStatus() const {
        std::lock_guard<std::mutex> lock(m_LoadMutex);
        if (m_LoadActive) {
            return "Loading";
        }
        if (m_LoadReaderDone) {
            if (m_LoadFailed) {
                return m_LoadStatus.empty() ? std::string("Failed") : (std::string("Failed: ") + m_LoadStatus);
            }
            return m_LoadStatus.empty() ? std::string("OK") : m_LoadStatus;
        }
        return "Idle";
    }

    RaycastHit ChunkManager::RaycastWorld(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
        RaycastHit result;

        glm::vec3 dir = glm::normalize(direction);
        if (!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z) || glm::length(dir) < 0.0001f) {
            return result;
        }

        glm::ivec3 cell = glm::ivec3(glm::floor(origin));
        glm::ivec3 prevCell = cell;

        const glm::ivec3 step(
            (dir.x >= 0.0f) ? 1 : -1,
            (dir.y >= 0.0f) ? 1 : -1,
            (dir.z >= 0.0f) ? 1 : -1
        );

        auto inv = [](float v) {
            return (std::abs(v) < 1e-8f) ? std::numeric_limits<float>::infinity() : (1.0f / v);
        };

        const glm::vec3 invDir(inv(dir.x), inv(dir.y), inv(dir.z));

        auto nextBoundary = [](float pos, int stepAxis) {
            return (stepAxis > 0) ? std::ceil(pos) : std::floor(pos);
        };

        glm::vec3 tMax(
            (nextBoundary(origin.x, step.x) - origin.x) * invDir.x,
            (nextBoundary(origin.y, step.y) - origin.y) * invDir.y,
            (nextBoundary(origin.z, step.z) - origin.z) * invDir.z
        );

        glm::vec3 tDelta(std::abs(invDir.x), std::abs(invDir.y), std::abs(invDir.z));

        float traveled = 0.0f;

        for (int i = 0; i < 4096; ++i) {
            const uint8_t b = GetBlockWorld(cell);
            if (b != 0) {
                result.hit = true;
                result.blockWorld = cell;
                result.prevWorld = prevCell;
                result.blockId = b;
                return result;
            }

            prevCell = cell;

            if (tMax.x < tMax.y) {
                if (tMax.x < tMax.z) {
                    traveled = tMax.x;
                    tMax.x += tDelta.x;
                    cell.x += step.x;
                } else {
                    traveled = tMax.z;
                    tMax.z += tDelta.z;
                    cell.z += step.z;
                }
            } else {
                if (tMax.y < tMax.z) {
                    traveled = tMax.y;
                    tMax.y += tDelta.y;
                    cell.y += step.y;
                } else {
                    traveled = tMax.z;
                    tMax.z += tDelta.z;
                    cell.z += step.z;
                }
            }

            if (traveled > maxDistance) break;
        }

        return result;
    }

    bool ChunkManager::GetChunkMeshStamp(const glm::ivec3& chunkCoord, std::uint64_t& outStamp) const {
        std::shared_lock<std::shared_mutex> lock(m_ChunksMutex);
        ChunkKey key{chunkCoord};
        auto it = m_Chunks.find(key);
        if (it == m_Chunks.end()) return false;
        outStamp = it->second.lastMeshStamp;
        return true;
    }

    std::size_t ChunkManager::GetInFlightGenerate() const {
        std::shared_lock<std::shared_mutex> lock(m_ChunksMutex);
        return m_InFlightGenerate;
    }

    std::size_t ChunkManager::GetInFlightRemesh() const {
        return m_InFlightRemesh.load(std::memory_order_relaxed);
    }

    std::size_t ChunkManager::GetCompletedCount() const {
        std::lock_guard<std::mutex> lock(m_CompletedMutex);
        return m_Completed.size();
    }

    std::size_t ChunkManager::GetDeferredRemeshCount() const {
        std::shared_lock<std::shared_mutex> lock(m_ChunksMutex);
        return m_RemeshDeferred.size();
    }

    std::string ChunkManager::GetChunkStoreFilePath(const glm::ivec3& chunkCoord) const {
        std::string root;
        {
            std::lock_guard<std::mutex> lock(m_LoadMutex);
            root = m_ChunkStoreRoot;
        }

        std::filesystem::path p = std::filesystem::path(root)
            / (std::string("x_") + std::to_string(chunkCoord.x))
            / (std::string("z_") + std::to_string(chunkCoord.z))
            / (std::string("y_") + std::to_string(chunkCoord.y) + ".chk");
        return p.string();
    }

    bool ChunkManager::TryLoadChunkFromStore(const glm::ivec3& chunkCoord, std::vector<uint8_t>& outBlocks) const {
        outBlocks.clear();
        const bool traceIoVerbose = GetEnvBool("HVE_TRACE_IO_VERBOSE", false);

        const std::string path = GetChunkStoreFilePath(chunkCoord);
        std::ifstream f(path.c_str(), std::ios::binary);
        if (!f.is_open()) {
            if (traceIoVerbose) {
                Engine::RunLog::Warn(std::string("IO ChunkLoad miss path=") + path);
            }
            return false;
        }

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t rawSize = 0;
        uint32_t rleSize = 0;
        uint32_t checksum = 0;
        f.read((char*)&magic, sizeof(uint32_t));
        f.read((char*)&version, sizeof(uint32_t));
        f.read((char*)&rawSize, sizeof(uint32_t));
        f.read((char*)&rleSize, sizeof(uint32_t));
        f.read((char*)&checksum, sizeof(uint32_t));
        if (!f.good()) return false;
        if (magic != kChunkBlobMagic || version != kChunkBlobVersion) return false;
        if (rawSize != (uint32_t)Chunk::BlockCount) return false;

        std::vector<uint8_t> rle;
        rle.resize((std::size_t)rleSize);
        if (rleSize > 0) {
            f.read((char*)rle.data(), (std::streamsize)rleSize);
            if (!f.good()) return false;
        }

        std::vector<uint8_t> decoded;
        if (!RLE_Decode(rle.data(), rle.size(), decoded, Chunk::BlockCount)) return false;

        const uint32_t computed = ComputeChunkChecksum(decoded.data(), decoded.size());
        if (computed != checksum) return false;

        outBlocks = std::move(decoded);
        if (traceIoVerbose) {
            Engine::RunLog::Info(std::string("IO ChunkLoad hit path=") + path);
        }
        return true;
    }

    void ChunkManager::SaveChunkToStore(const glm::ivec3& chunkCoord, const std::vector<uint8_t>& blocks) const {
        if (blocks.size() != Chunk::BlockCount) return;
        const bool traceIoVerbose = GetEnvBool("HVE_TRACE_IO_VERBOSE", false);

        const std::string target = GetChunkStoreFilePath(chunkCoord);
        const std::string temp = target + ".tmp";

        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(target).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            ec.clear();
        }

        std::vector<uint8_t> rle;
        RLE_Encode(blocks.data(), blocks.size(), rle);
        const uint32_t rawSize = (uint32_t)blocks.size();
        const uint32_t rleSize = (uint32_t)rle.size();
        const uint32_t checksum = ComputeChunkChecksum(blocks.data(), blocks.size());

        {
            std::ofstream f(temp.c_str(), std::ios::binary | std::ios::trunc);
            if (!f.is_open()) return;

            f.write((const char*)&kChunkBlobMagic, sizeof(uint32_t));
            f.write((const char*)&kChunkBlobVersion, sizeof(uint32_t));
            f.write((const char*)&rawSize, sizeof(uint32_t));
            f.write((const char*)&rleSize, sizeof(uint32_t));
            f.write((const char*)&checksum, sizeof(uint32_t));
            if (rleSize > 0) {
                f.write((const char*)rle.data(), (std::streamsize)rleSize);
            }
            f.flush();
            if (!f.good()) {
                f.close();
                std::filesystem::remove(temp, ec);
                return;
            }
        }

        if (!MoveOrReplaceFile(temp, target)) {
            std::filesystem::remove(temp, ec);
            if (traceIoVerbose) {
                Engine::RunLog::Error(std::string("IO ChunkSave failed target=") + target);
            }
        } else if (traceIoVerbose) {
            Engine::RunLog::Info(std::string("IO ChunkSave done target=") + target);
        }
    }

    void ChunkManager::SetChunkStoreRootFromWorldPath(const char* worldPath) {
        std::string root = GetEnvString("HVE_CHUNK_STORE_ROOT", "minecraft/world_chunks");

        if (worldPath && *worldPath) {
            std::error_code ec;
            std::filesystem::path world(worldPath);
            const std::filesystem::path parent = world.parent_path();
            const std::string stem = world.stem().string();

            if (!stem.empty()) {
                if (parent.empty()) {
                    root = (std::filesystem::path(stem + ".chunks")).string();
                } else {
                    root = (parent / (stem + ".chunks")).string();
                }
            }

            std::filesystem::create_directories(std::filesystem::path(root), ec);
            ec.clear();
        }

        std::lock_guard<std::mutex> lock(m_LoadMutex);
        m_ChunkStoreRoot = root;
    }

}}
