#pragma once

#include "Game/World/Chunk.h"

#include <glm/glm.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Threading { class ThreadPool; }

namespace Engine { class WorldRenderer; }

namespace Game { namespace World {

    class SparseVoxelOctree;

    struct ChunkKey {
        glm::ivec3 coord{0};
        bool operator==(const ChunkKey& other) const { return coord == other.coord; }
    };

    struct ChunkKeyHash {
        std::size_t operator()(const ChunkKey& k) const noexcept {
            // 3D integer hash
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

    struct ChunkMeshCPU {
        ChunkKey key;
        glm::ivec3 chunkCoord{0};
        std::vector<uint8_t> blocks; // 32^3
        std::vector<uint32_t> vertices;
        std::vector<unsigned int> indices;
        bool hasBlocks = false;
    };

    struct RaycastHit {
        bool hit = false;
        glm::ivec3 blockWorld{0};
        glm::ivec3 prevWorld{0};
        uint8_t blockId = 0;
    };

    class ChunkManager {
    public:
        ChunkManager();
        ~ChunkManager();

        void Init(Threading::ThreadPool* pool);

        // Streaming: ensure chunks exist around player (XZ + a fixed number of vertical layers starting at Y=0)
        void UpdateStreaming(const glm::vec3& playerWorldPos, int viewDistance, int heightChunks);

        // Massive one-shot startup prewarm around a center point.
        // This aggressively queues chunk generation in a 3D radius with internal budgeting.
        void PreloadLargeArea(const glm::vec3& centerPos, int horizontalRadius, int verticalRadius);

        // Unload chunks outside view distance and remove them from the renderer.
        void UnloadFarChunks(const glm::vec3& playerWorldPos, int viewDistance, int heightChunks, Engine::WorldRenderer& renderer);

        // Apply completed worker jobs on the main thread and upload to renderer.
        // maxUploads limits how many chunks are uploaded per frame to prevent stutter.
        // Increased to 16 to fill holes faster since stutter is less of an issue than missing geometry.
        void PumpCompleted(Engine::WorldRenderer& renderer, int maxUploads = 8);

        // Persistence (main thread): save/load all currently loaded chunks.
        // File format is engine-private and may change; meant for local worlds.
        void ClearAll(Engine::WorldRenderer& renderer);
        bool SaveWorldToFile(const char* path) const;
        bool LoadWorldFromFile(const char* path, Engine::WorldRenderer& renderer);

        // Async load: designed for huge worlds. Reader runs on a worker thread and
        // decoded chunks are imported on the main thread in small batches.
        bool BeginLoadWorldFromFileAsync(const char* path, Engine::WorldRenderer& renderer);
        void PumpAsyncLoad(Engine::WorldRenderer& renderer, int maxChunksPerFrame = 8);
        bool IsAsyncLoadInProgress() const;
        bool IsAsyncLoadFinished() const;
        float GetAsyncLoadProgress01() const; // 0..1 (read progress)
        std::string GetAsyncLoadStatus() const;

        // World block access (main thread)
        uint8_t GetBlockWorld(const glm::ivec3& world) const;
        void SetBlockWorld(const glm::ivec3& world, uint8_t id);
        void BeginBulkEdit();
        void EndBulkEdit();

        // Raycast against loaded chunks (main thread)
        RaycastHit RaycastWorld(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;

        // Debug/diagnostics: read last mesh upload stamp for a chunk.
        bool GetChunkMeshStamp(const glm::ivec3& chunkCoord, std::uint64_t& outStamp) const;

        // Perf diagnostics
        std::size_t GetInFlightGenerate() const;
        std::size_t GetInFlightRemesh() const;
        std::size_t GetCompletedCount() const;
        std::size_t GetDeferredRemeshCount() const;

        // Mark a chunk dirty and schedule a remesh job
        void RequestRemesh(const glm::ivec3& chunkCoord);

    private:
        struct ChunkRecord {
            Chunk chunk;
            bool ready = false;
            bool meshingQueued = false;
            bool needsRemesh = false;
            bool remeshDeferred = false;
            std::uint64_t lastMeshStamp = 0;

            explicit ChunkRecord(const glm::ivec3& coord) : chunk(coord) {}
        };

        glm::ivec3 WorldToChunkCoord(const glm::ivec3& world) const;
        glm::ivec3 WorldToLocal(const glm::ivec3& world, const glm::ivec3& chunkCoord) const;

        void QueueGenerateAndMesh(const glm::ivec3& chunkCoord);
        void QueueRemeshCopy(const glm::ivec3& chunkCoord);
        void EnqueueRemeshJob(const glm::ivec3& chunkCoord);
        void RequestRemeshLocked(const glm::ivec3& chunkCoord);
        void QueueRemeshNeighborhood27Locked(const glm::ivec3& centerCoord);
        void CopyPaddedForRemesh(const glm::ivec3& chunkCoord, std::vector<uint8_t>& out) const;
        std::string GetChunkStoreFilePath(const glm::ivec3& chunkCoord) const;
        bool TryLoadChunkFromStore(const glm::ivec3& chunkCoord, std::vector<uint8_t>& outBlocks) const;
        void SaveChunkToStore(const glm::ivec3& chunkCoord, const std::vector<uint8_t>& blocks) const;
        void SetChunkStoreRootFromWorldPath(const char* worldPath);

        Threading::ThreadPool* m_Pool = nullptr;

        mutable std::shared_mutex m_ChunksMutex;
        std::atomic<std::size_t> m_InFlightGenerate{0};
        std::atomic<std::size_t> m_InFlightRemesh{0};
        std::uint64_t m_MeshStampCounter = 0;

        std::unordered_map<ChunkKey, ChunkRecord, ChunkKeyHash> m_Chunks;

        // Completed jobs from workers
        mutable std::mutex m_CompletedMutex;
        std::vector<ChunkMeshCPU> m_Completed;

        std::deque<glm::ivec3> m_RemeshDeferred;
        int m_BulkEditDepth = 0;
        std::unordered_set<ChunkKey, ChunkKeyHash> m_BulkDirtyChunks;
        glm::vec3 m_LastStreamPlayerPos{0.0f};
        glm::vec2 m_StreamDirXZ{0.0f};
        bool m_HasLastStreamPos = false;
        std::unique_ptr<SparseVoxelOctree> m_SVO;

        // Async world loading state
        struct LoadedChunk {
            glm::ivec3 coord{0};
            std::vector<uint8_t> blocks;
        };

        mutable std::mutex m_LoadMutex;
        std::deque<LoadedChunk> m_LoadQueue;
        bool m_LoadActive = false;
        bool m_LoadReaderDone = false;
        bool m_LoadFailed = false;
        std::string m_LoadStatus;
        std::uint32_t m_LoadTotal = 0;
        std::uint32_t m_LoadRead = 0;
        std::string m_ChunkStoreRoot;
    };

}}
