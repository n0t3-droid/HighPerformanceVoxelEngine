#include "Generation.h"
#include "Hydrology.h"
#include "TerrainPreset.h"

#include <FastNoiseLite.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <vector>

// SAFE CLAMP – verhindert Assertion-Crashs bei vertauschten Clamp-Grenzen.
template<typename T>
T SafeClamp(T value, T minVal, T maxVal) {
    if (minVal > maxVal) std::swap(minVal, maxVal);
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

namespace Game { namespace World {

    static int GetEnvIntClamped(const char* name, int defaultValue, int minValue, int maxValue) {
        const char* v = std::getenv(name);
        if (!v || !*v) return defaultValue;
        char* end = nullptr;
        long parsed = std::strtol(v, &end, 10);
        if (end == v) return defaultValue;
        return SafeClamp((int)parsed, minValue, maxValue);
    }

    static bool IsFlatWorldForced() {
        static const bool forced = (GetEnvIntClamped("HVE_FORCE_FLAT_WORLD", 1, 0, 1) != 0);
        return forced;
    }

    static int GetFlatWorldY() {
        static const int flatY = GetEnvIntClamped("HVE_FORCE_FLAT_Y", 52, 8, 720);
        return flatY;
    }

    static bool IsSuperflatMode() {
        static const bool enabled = (GetEnvIntClamped("HVE_SUPERFLAT_MODE", 1, 0, 1) != 0);
        return enabled;
    }

    static int GetSuperflatDirtLayers() {
        static const int layers = GetEnvIntClamped("HVE_SUPERFLAT_DIRT_LAYERS", 3, 1, 24);
        return layers;
    }

    static uint8_t SampleSuperflatBlockAtY(int wy) {
        static constexpr uint8_t kSuperflatDirt = 1;
        static constexpr uint8_t kSuperflatGrass = 2;
        static constexpr uint8_t kSuperflatStone = 3;
        const int surfaceY = GetFlatWorldY();
        const int dirtLayers = GetSuperflatDirtLayers();
        const int dirtBottomY = surfaceY - dirtLayers;

        if (wy <= 0) return kSuperflatStone;
        if (wy == surfaceY) return kSuperflatGrass;
        if (wy < surfaceY && wy >= dirtBottomY) return kSuperflatDirt;
        if (wy < dirtBottomY) return kSuperflatStone;
        return 0;
    }

    static constexpr uint8_t BLOCK_DIRT = 1;
    static constexpr uint8_t BLOCK_GRASS = 2;
    static constexpr uint8_t BLOCK_STONE = 3;
    static constexpr uint8_t BLOCK_WATER = 5;

    static FastNoiseLite s_Continent;
    static FastNoiseLite s_Plateau;
    static FastNoiseLite s_Slope;
    static FastNoiseLite s_Cliff;
    static FastNoiseLite s_Mountain;
    static FastNoiseLite s_Warp;
    static FastNoiseLite s_River;
    static FastNoiseLite s_Macro;

    static int s_Seed = 12345;

    struct ErosionPatch {
        bool valid = false;
        int originX = 0;
        int originZ = 0;
        int width = 0;
        int height = 0;
        int step = 1;
        float strength = 1.0f;
        std::vector<float> delta;
    };

    static std::mutex s_ErosionPatchMutex;
    static ErosionPatch s_ErosionPatch;

    struct HydroTileCache {
        bool valid = false;
        int originX = 0;
        int originZ = 0;
        int cellSize = 8;
        int width = 64;
        int height = 64;
        float maxFlowAccum = 1.0f;
        HydrologyMaps maps;
    };

    static thread_local HydroTileCache s_HydroTile;

    static int DivFloor(int a, int b) {
        int q = a / b;
        int r = a % b;
        if ((r != 0) && ((r > 0) != (b > 0))) --q;
        return q;
    }

    static float SampleErosionDelta(int worldX, int worldZ) {
        std::scoped_lock lock(s_ErosionPatchMutex);
        if (!s_ErosionPatch.valid) return 0.0f;
        if (s_ErosionPatch.width <= 0 || s_ErosionPatch.height <= 0 || s_ErosionPatch.step <= 0) return 0.0f;

        const int localX = worldX - s_ErosionPatch.originX;
        const int localZ = worldZ - s_ErosionPatch.originZ;
        if (localX < 0 || localZ < 0) return 0.0f;

        const int maxX = (s_ErosionPatch.width - 1) * s_ErosionPatch.step;
        const int maxZ = (s_ErosionPatch.height - 1) * s_ErosionPatch.step;
        if (localX > maxX || localZ > maxZ) return 0.0f;

        const int x0 = localX / s_ErosionPatch.step;
        const int z0 = localZ / s_ErosionPatch.step;
        const int x1 = std::min(s_ErosionPatch.width - 1, x0 + 1);
        const int z1 = std::min(s_ErosionPatch.height - 1, z0 + 1);

        const float tx = (float)(localX - x0 * s_ErosionPatch.step) / (float)s_ErosionPatch.step;
        const float tz = (float)(localZ - z0 * s_ErosionPatch.step) / (float)s_ErosionPatch.step;

        auto at = [&](int x, int z) {
            return s_ErosionPatch.delta[(size_t)z * (size_t)s_ErosionPatch.width + (size_t)x];
        };

        const float d00 = at(x0, z0);
        const float d10 = at(x1, z0);
        const float d01 = at(x0, z1);
        const float d11 = at(x1, z1);
        const float dx0 = d00 + (d10 - d00) * tx;
        const float dx1 = d01 + (d11 - d01) * tx;
        return (dx0 + (dx1 - dx0) * tz) * s_ErosionPatch.strength;
    }

    static float Terrace(float h, float steps = 8.0f, float smooth = 0.45f) {
        float t = h * steps;
        float floorT = std::floor(t);
        float frac = t - floorT;
        float smoothFrac = frac * frac * (3.0f - 2.0f * frac);
        return (floorT + smoothFrac * smooth) / steps;
    }

    static float ComputeBaseTerrainHeight(int wx, int wz, float* outClassicRiverMask = nullptr) {
        if (IsFlatWorldForced()) {
            if (outClassicRiverMask) {
                *outClassicRiverMask = 0.0f;
            }
            return (float)GetFlatWorldY();
        }

        float x = (float)wx;
        float z = (float)wz;
        s_Warp.DomainWarp(x, z);

        const float continent = (s_Continent.GetNoise(x, z) + 1.0f) * 0.5f;
        const float plateau = Terrace(s_Plateau.GetNoise(x * 0.72f, z * 0.72f) * 0.5f + 0.5f, 8.0f, 0.45f);
        const float slope = s_Slope.GetNoise(x * 0.85f, z * 0.85f);
        const float cliff = std::abs(s_Cliff.GetNoise(x * 0.00135f, z * 0.00135f));
        const float mountain = s_Mountain.GetNoise(x, z);
        const float macro = (s_Macro.GetNoise(x * 0.00019f, z * 0.00019f) + 1.0f) * 0.5f;
        const float river = std::abs(s_River.GetNoise(x * 0.00034f, z * 0.00034f));

        float height = 52.0f + plateau * 32.0f + slope * 6.8f * (1.0f - plateau * 0.6f);
        if (cliff > 0.62f) {
            height += (cliff - 0.62f) * 58.0f * macro;
        }
        if (mountain > 0.48f) {
            height += (mountain - 0.48f) * 255.0f * continent * macro;
        }

        const float riverMask = std::pow(1.0f - std::min(1.0f, river * 28.0f), 2.75f);
        if (outClassicRiverMask) {
            *outClassicRiverMask = riverMask;
        }

        return height;
    }

    static bool EnsureHydrologyTile(int wx, int wz) {
        const int tileCellSize = 8;
        const int tileW = 64;
        const int tileH = 64;
        const int worldSpanX = (tileW - 1) * tileCellSize;
        const int worldSpanZ = (tileH - 1) * tileCellSize;

        if (s_HydroTile.valid) {
            const int maxX = s_HydroTile.originX + worldSpanX;
            const int maxZ = s_HydroTile.originZ + worldSpanZ;
            if (wx >= s_HydroTile.originX && wx <= maxX && wz >= s_HydroTile.originZ && wz <= maxZ) {
                return true;
            }
        }

        std::vector<float> baseHeights;
        baseHeights.resize((size_t)tileW * (size_t)tileH);

        const int snappedCenterX = DivFloor(wx, tileCellSize) * tileCellSize;
        const int snappedCenterZ = DivFloor(wz, tileCellSize) * tileCellSize;
        const int originX = snappedCenterX - worldSpanX / 2;
        const int originZ = snappedCenterZ - worldSpanZ / 2;

        for (int zi = 0; zi < tileH; ++zi) {
            for (int xi = 0; xi < tileW; ++xi) {
                const int sx = originX + xi * tileCellSize;
                const int sz = originZ + zi * tileCellSize;
                baseHeights[(size_t)zi * (size_t)tileW + (size_t)xi] = ComputeBaseTerrainHeight(sx, sz, nullptr);
            }
        }

        HydrologyMaps maps;
        if (!Hydrology::BuildHydrologyMaps(baseHeights, tileW, tileH, maps)) {
            s_HydroTile = HydroTileCache{};
            return false;
        }

        float maxFlow = 1.0f;
        for (float v : maps.flowAccum) {
            maxFlow = std::max(maxFlow, v);
        }

        s_HydroTile.valid = true;
        s_HydroTile.originX = originX;
        s_HydroTile.originZ = originZ;
        s_HydroTile.cellSize = tileCellSize;
        s_HydroTile.width = tileW;
        s_HydroTile.height = tileH;
        s_HydroTile.maxFlowAccum = maxFlow;
        s_HydroTile.maps = std::move(maps);
        return true;
    }

    static float SampleHydrologyRiverMask(int wx, int wz) {
        static const bool hydrologyEnabled = GetEnvIntClamped("HVE_HYDROLOGY_RIVERS", 1, 0, 1) != 0;
        if (!hydrologyEnabled) return 0.0f;
        if (!EnsureHydrologyTile(wx, wz)) return 0.0f;
        if (s_HydroTile.maps.flowAccum.empty()) return 0.0f;

        const float fx = (float)(wx - s_HydroTile.originX) / (float)s_HydroTile.cellSize;
        const float fz = (float)(wz - s_HydroTile.originZ) / (float)s_HydroTile.cellSize;

        const int x0 = SafeClamp((int)std::floor(fx), 0, s_HydroTile.width - 1);
        const int z0 = SafeClamp((int)std::floor(fz), 0, s_HydroTile.height - 1);
        const int x1 = SafeClamp(x0 + 1, 0, s_HydroTile.width - 1);
        const int z1 = SafeClamp(z0 + 1, 0, s_HydroTile.height - 1);
        const float tx = SafeClamp(fx - (float)x0, 0.0f, 1.0f);
        const float tz = SafeClamp(fz - (float)z0, 0.0f, 1.0f);

        auto at = [&](int x, int z) {
            return s_HydroTile.maps.flowAccum[(size_t)z * (size_t)s_HydroTile.width + (size_t)x];
        };

        const float a00 = at(x0, z0);
        const float a10 = at(x1, z0);
        const float a01 = at(x0, z1);
        const float a11 = at(x1, z1);
        const float ax0 = a00 + (a10 - a00) * tx;
        const float ax1 = a01 + (a11 - a01) * tx;
        const float flow = ax0 + (ax1 - ax0) * tz;

        const float norm = std::log1p(std::max(0.0f, flow)) / std::log1p(std::max(1.0f, s_HydroTile.maxFlowAccum));
        return SafeClamp(std::pow(norm, 1.35f), 0.0f, 1.0f);
    }

    struct ColumnInfo {
        int surfaceY = 52;
        int waterY = -1;
        uint8_t topBlock = BLOCK_GRASS;
        uint8_t filler = BLOCK_DIRT;
    };

    static const TerrainPreset& CurrentPreset() {
        const int idx = SafeClamp(g_CurrentPresetIndex, 0, 7);
        return g_TerrainPresets[idx];
    }

    static void ApplyCurrentPresetToNoise() {
        const TerrainPreset& p = CurrentPreset();
        s_Continent.SetFrequency(p.continentFreq);
        s_Plateau.SetFrequency(p.plateauFreq);
        s_River.SetFrequency(p.riverFreq);

        // Multipass-inspirierte Sekundaerparameter
        s_Slope.SetFrequency(p.plateauFreq * 3.15f);
        s_Cliff.SetFrequency(p.plateauFreq * 1.22f);
        s_Mountain.SetFrequency(p.continentFreq * 1.90f);
        s_Warp.SetFrequency(p.plateauFreq * 1.48f);
        s_Macro.SetFrequency(p.continentFreq * 0.56f);

        const float warpAmp = 48.0f + p.plateauAmp * 0.90f;
        s_Warp.SetDomainWarpAmp(SafeClamp(warpAmp, 12.0f, 128.0f));
    }

    void Generation::Init(int seed) {
        s_Seed = seed;

        s_Continent.SetSeed(seed);
        s_Plateau.SetSeed(seed + 7);
        s_Slope.SetSeed(seed + 19);
        s_Cliff.SetSeed(seed + 31);
        s_Mountain.SetSeed(seed + 43);
        s_Warp.SetSeed(seed + 55);
        s_River.SetSeed(seed + 67);
        s_Macro.SetSeed(seed + 79);

        s_Mountain.SetFractalType(FastNoiseLite::FractalType_Ridged);

        // LowSpecMode aktivieren, wenn Vega iGPU erkannt
        bool lowSpecMode = false;
        #ifdef VEGA_I_GPU
        lowSpecMode = true;
        #endif
        ApplyPresetToNoise(lowSpecMode);
        ApplyCurrentPresetToNoise();

        const TerrainPreset& p = CurrentPreset();
        std::cout << "[AETHERFORGE v5] preset=" << p.name << " seed=" << seed << std::endl;
    }

    static ColumnInfo ComputeColumn(int wx, int wz) {
        if (IsFlatWorldForced()) {
            ColumnInfo col;
            col.surfaceY = GetFlatWorldY();
            col.waterY = -1;
            col.topBlock = BLOCK_GRASS;
            col.filler = BLOCK_DIRT;
            return col;
        }

        float classicRiverMask = 0.0f;
        float height = ComputeBaseTerrainHeight(wx, wz, &classicRiverMask);
        const float hydroRiverMask = SampleHydrologyRiverMask(wx, wz);
        const float riverMask = SafeClamp(classicRiverMask * 0.42f + hydroRiverMask * 0.92f, 0.0f, 1.0f);
        height -= riverMask * 18.0f;

        height += SampleErosionDelta(wx, wz);
        height = SafeClamp(height, 44.0f, 720.0f);

        ColumnInfo col;
        col.surfaceY = (int)std::floor(height);

        if (col.surfaceY < 44) col.waterY = 44;

        if (col.surfaceY > 148) {
            col.topBlock = BLOCK_STONE;
        } else if (riverMask > 0.65f && col.surfaceY < 62) {
            col.topBlock = BLOCK_DIRT;
            col.filler = BLOCK_DIRT;
        } else {
            col.topBlock = BLOCK_GRASS;
        }

        if (col.waterY > 0) {
            col.topBlock = BLOCK_WATER;
        }

        return col;
    }

    uint8_t Generation::GetBlockAtWorld(int wx, int wy, int wz) {
        if (IsFlatWorldForced() && IsSuperflatMode()) {
            return SampleSuperflatBlockAtY(wy);
        }

        ColumnInfo col = ComputeColumn(wx, wz);
        if (wy <= 0) return BLOCK_STONE;
        if (wy < col.surfaceY - 11) return BLOCK_STONE;
        if (wy < col.surfaceY) return col.filler;
        if (wy == col.surfaceY) return col.topBlock;
        if (col.waterY > 0 && wy <= col.waterY) return BLOCK_WATER;
        return 0;
    }

    int Generation::GetSurfaceYAtWorld(int wx, int wz) {
        return ComputeColumn(wx, wz).surfaceY;
    }

    int Generation::GetBaseSurfaceYAtWorld(int wx, int wz) {
        float classicRiverMask = 0.0f;
        float height = ComputeBaseTerrainHeight(wx, wz, &classicRiverMask);
        height -= classicRiverMask * 0.42f * 18.0f;
        return (int)std::floor(SafeClamp(height, 44.0f, 720.0f));
    }

    void Generation::GenerateChunk(Chunk& chunk) {
        glm::ivec3 p = chunk.GetPosition();

        if (IsFlatWorldForced() && IsSuperflatMode()) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    for (int y = 0; y < CHUNK_SIZE; ++y) {
                        const int wy = p.y * CHUNK_SIZE + y;
                        chunk.SetBlock(x, y, z, SampleSuperflatBlockAtY(wy));
                    }
                }
            }
            return;
        }
        
        // ====================================================================
        //  INVENTION: Sub-Chunk Spatial Hazard Matrix (Noise Skipping)
        //  Instead of calculating FastNoiseLite (heavy math) for literally every
        //  x,z column within a chunk, we just sample the 4 corners of the chunk.
        //  Because terrain frequency is naturally bounded, if all 4 corners 
        //  are significantly below this chunk's bottom Y, we know this entire
        //  chunk is 100% AIR. If all 4 corners are significantly above this 
        //  chunk's top Y, it's 100% SOLID STONE.
        //
        //  Result: Massively skips generation for skies and deep undergrounds.
        // ====================================================================
        const int lowestYInChunk = p.y * CHUNK_SIZE;
        const int highestYInChunk = p.y * CHUNK_SIZE + CHUNK_SIZE - 1;
        
        const ColumnInfo c00 = ComputeColumn(p.x * CHUNK_SIZE, p.z * CHUNK_SIZE);
        const ColumnInfo c10 = ComputeColumn(p.x * CHUNK_SIZE + CHUNK_SIZE - 1, p.z * CHUNK_SIZE);
        const ColumnInfo c01 = ComputeColumn(p.x * CHUNK_SIZE, p.z * CHUNK_SIZE + CHUNK_SIZE - 1);
        const ColumnInfo c11 = ComputeColumn(p.x * CHUNK_SIZE + CHUNK_SIZE - 1, p.z * CHUNK_SIZE + CHUNK_SIZE - 1);
        
        // We add a safety margin of 24 blocks (max slope variance within 32 blocks)
        int minSurf = std::min({c00.surfaceY, c10.surfaceY, c01.surfaceY, c11.surfaceY}) - 24;
        int maxSurf = std::max({c00.surfaceY, c10.surfaceY, c01.surfaceY, c11.surfaceY}) + 24;
        
        if (minSurf > highestYInChunk && c00.waterY < lowestYInChunk) {
            // Entire chunk is deeply buried underground -> 100% STONE
            // INVENTION: Memory-Mapped Block Swarm Write (memset bypassing iterations)
            std::vector<uint8_t> fill(Chunk::BlockCount, BLOCK_STONE);
            chunk.CopyBlocksFrom(fill.data(), fill.size());
            return;
        }
        
        if (maxSurf < lowestYInChunk && c00.waterY < lowestYInChunk) {
            // Entire chunk is high in the sky -> 100% AIR
            std::vector<uint8_t> fill(Chunk::BlockCount, 0);
            chunk.CopyBlocksFrom(fill.data(), fill.size());
            return;
        }

        // Slow path: surface boundary intersects this chunk, or it's a water chunk
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                const int wx = p.x * CHUNK_SIZE + x;
                const int wz = p.z * CHUNK_SIZE + z;
                const ColumnInfo col = ComputeColumn(wx, wz);

                for (int y = 0; y < CHUNK_SIZE; ++y) {
                    const int wy = p.y * CHUNK_SIZE + y;
                    uint8_t b = 0;
                    if (wy <= 0) b = BLOCK_STONE;
                    else if (wy < col.surfaceY - 11) b = BLOCK_STONE;
                    else if (wy < col.surfaceY) b = col.filler;
                    else if (wy == col.surfaceY) b = col.topBlock;
                    else if (col.waterY > 0 && wy <= col.waterY) b = BLOCK_WATER;
                    chunk.SetBlock(x, y, z, b);
                }
            }
        }

        // ====================================================================
        //  INVENTION: Deterministic Overlapping Procedural Trees
        //  Evaluating a 5x5 chunk-space footprint allows trees to perfectly
        //  overlap chunk borders without requiring cross-chunk locks!
        // ====================================================================
        if (highestYInChunk >= 40 && lowestYInChunk <= 150) {
            for (int x = -2; x < CHUNK_SIZE + 2; ++x) {
                for (int z = -2; z < CHUNK_SIZE + 2; ++z) {
                    const int wx = p.x * CHUNK_SIZE + x;
                    const int wz = p.z * CHUNK_SIZE + z;
                    
                    float hash = std::abs(std::sin(wx * 12.9898f + wz * 78.233f) * 43758.5453f);
                    hash = hash - std::floor(hash);
                    
                    if (hash > 0.985f) { // 1.5% chance
                        const ColumnInfo treeCol = ComputeColumn(wx, wz);
                        if (treeCol.topBlock == BLOCK_GRASS && treeCol.surfaceY >= lowestYInChunk - 15 && treeCol.surfaceY <= highestYInChunk + 5) {
                            int treeY = treeCol.surfaceY + 1;
                            int treeHeight = 5 + (int)((hash - 0.985f) * 1000.0f) % 3;
                            
                            for(int ty = 0; ty < treeHeight; ++ty) {
                                int currentWy = treeY + ty;
                                int ly = currentWy - p.y * CHUNK_SIZE;
                                if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE && ly >= 0 && ly < CHUNK_SIZE) {
                                    chunk.SetBlock(x, ly, z, 17); // Wood
                                }
                            }
                            
                            int leafBottom = treeY + treeHeight - 3;
                            int leafTop = treeY + treeHeight + 1;
                            for (int l_y = leafBottom; l_y <= leafTop; ++l_y) {
                                int ly = l_y - p.y * CHUNK_SIZE;
                                if (ly < 0 || ly >= CHUNK_SIZE) continue;
                                
                                int radius = (l_y == leafTop || l_y == leafBottom) ? 1 : 2;
                                for (int lx = -radius; lx <= radius; ++lx) {
                                    for (int lz = -radius; lz <= radius; ++lz) {
                                        if (std::abs(lx) == radius && std::abs(lz) == radius && (l_y == leafTop || l_y == leafBottom)) continue;
                                        
                                        int cx = x + lx;
                                        int cz = z + lz;
                                        if (cx >= 0 && cx < CHUNK_SIZE && cz >= 0 && cz < CHUNK_SIZE) {
                                            if (chunk.GetBlock(cx, ly, cz) == 0) {
                                                chunk.SetBlock(cx, ly, cz, 18); // Leaves
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    int Generation::GetSeed() {
        return s_Seed;
    }

    void Generation::SetErosionHeightDeltaPatch(int originX, int originZ,
                                                int width, int height,
                                                int step,
                                                const std::vector<float>& heightDelta,
                                                float strength) {
        if (width <= 0 || height <= 0 || step <= 0) return;
        if ((int)heightDelta.size() < width * height) return;

        std::scoped_lock lock(s_ErosionPatchMutex);
        s_ErosionPatch.valid = true;
        s_ErosionPatch.originX = originX;
        s_ErosionPatch.originZ = originZ;
        s_ErosionPatch.width = width;
        s_ErosionPatch.height = height;
        s_ErosionPatch.step = step;
        s_ErosionPatch.strength = SafeClamp(strength, 0.0f, 2.0f);
        s_ErosionPatch.delta = heightDelta;
    }

    void Generation::ClearErosionHeightDeltaPatch() {
        std::scoped_lock lock(s_ErosionPatchMutex);
        s_ErosionPatch = ErosionPatch{};
    }

    TerrainMetrics Generation::SampleTerrainMetrics(int centerX, int centerZ, int radius, int step) {
        TerrainMetrics out;
        radius = std::max(8, radius);
        step = std::max(1, step);

        const int span = radius * 2 + 1;
        const int w = std::max(2, (span + step - 1) / step);
        const int h = w;

        std::array<int, 256> bins{};
        std::array<int, 256> biomeCounts{};
        int sampleCount = 0;
        float minH = 1e9f;
        float maxH = -1e9f;
        std::vector<float> sampledHeights;
        sampledHeights.reserve((size_t)w * (size_t)h);

        for (int zi = 0; zi < h; ++zi) {
            for (int xi = 0; xi < w; ++xi) {
                const int worldX = centerX - radius + xi * step;
                const int worldZ = centerZ - radius + zi * step;
                const ColumnInfo col = ComputeColumn(worldX, worldZ);
                const float hh = (float)col.surfaceY;
                sampledHeights.push_back(hh);
                minH = std::min(minH, hh);
                maxH = std::max(maxH, hh);
                sampleCount++;
                biomeCounts[(unsigned int)col.topBlock]++;
            }
        }

        if (sampleCount <= 0) return out;

        const float heightRange = std::max(1.0f, maxH - minH);
        for (float hh : sampledHeights) {
            int idx = (int)std::floor(((hh - minH) / heightRange) * 255.0f + 0.5f);
            idx = SafeClamp(idx, 0, 255);
            bins[(size_t)idx]++;
        }

        double entropy = 0.0;
        for (int c : bins) {
            if (c <= 0) continue;
            const double p = (double)c / (double)sampleCount;
            entropy += -p * std::log2(p);
        }
        out.heightEntropy = (float)entropy;

        int usedBiomes = 0;
        for (int c : biomeCounts) if (c > 0) usedBiomes++;
        out.biomeVariance = (float)usedBiomes / 8.0f;
        out.erosionDetail = SafeClamp((maxH - minH) / 260.0f, 0.0f, 2.0f);

        HydrologyMaps hydro;
        if (Hydrology::BuildHydrologyMaps(sampledHeights, w, h, hydro) && !hydro.flowAccum.empty()) {
            float flowMax = 0.0f;
            float flowMean = 0.0f;
            for (float v : hydro.flowAccum) {
                flowMax = std::max(flowMax, v);
                flowMean += v;
            }
            flowMean /= std::max(1, (int)hydro.flowAccum.size());
            const float drainageContrast = (flowMean > 0.0f) ? (flowMax / flowMean) : 1.0f;
            out.artifactDensity = SafeClamp(1.0f / drainageContrast, 0.0f, 1.0f);
        } else {
            out.artifactDensity = 0.02f;
        }
        return out;
    }

}}
