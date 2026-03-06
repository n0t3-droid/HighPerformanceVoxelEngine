#pragma once

#include "Chunk.h"

#include <vector>

namespace Game { namespace World {
    struct TerrainMetrics {
        float heightEntropy = 0.0f;
        float biomeVariance = 0.0f;
        float erosionDetail = 0.0f;
        float artifactDensity = 1.0f;
    };

    class Generation {
    public:
        static void Init(int seed);
        static int GetSeed();
        static void GenerateChunk(Chunk& chunk);

        // Deterministic sampling for a single voxel in world space.
        // Used to build seam-free meshes even when neighbor chunks are not loaded.
        static uint8_t GetBlockAtWorld(int worldX, int worldY, int worldZ);

        static int GetSurfaceYAtWorld(int worldX, int worldZ);
        static int GetBaseSurfaceYAtWorld(int worldX, int worldZ);

        static void SetErosionHeightDeltaPatch(int originX, int originZ,
                               int width, int height,
                               int step,
                               const std::vector<float>& heightDelta,
                               float strength = 1.0f);
        static void ClearErosionHeightDeltaPatch();

        // Computes coarse realism metrics over a square patch around (centerX, centerZ).
        static TerrainMetrics SampleTerrainMetrics(int centerX, int centerZ, int radius = 32, int step = 2);
    };
}}
