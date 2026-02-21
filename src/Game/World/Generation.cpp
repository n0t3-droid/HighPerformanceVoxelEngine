#include "Generation.h"
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

        ApplyPresetToNoise();
        ApplyCurrentPresetToNoise();

        const TerrainPreset& p = CurrentPreset();
        std::cout << "[AETHERFORGE v5] preset=" << p.name << " seed=" << seed << std::endl;
    }

    static ColumnInfo ComputeColumn(int wx, int wz) {
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

        float riverMask = std::pow(1.0f - std::min(1.0f, river * 28.0f), 2.75f);
        height -= riverMask * 14.0f;

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

    void Generation::GenerateChunk(Chunk& chunk) {
        glm::ivec3 p = chunk.GetPosition();
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

        for (int zi = 0; zi < h; ++zi) {
            for (int xi = 0; xi < w; ++xi) {
                const int worldX = centerX - radius + xi * step;
                const int worldZ = centerZ - radius + zi * step;
                const ColumnInfo col = ComputeColumn(worldX, worldZ);
                const float hh = (float)col.surfaceY;
                minH = std::min(minH, hh);
                maxH = std::max(maxH, hh);
                sampleCount++;
                biomeCounts[(unsigned int)col.topBlock]++;
            }
        }

        if (sampleCount <= 0) return out;

        const float heightRange = std::max(1.0f, maxH - minH);
        for (int zi = 0; zi < h; ++zi) {
            for (int xi = 0; xi < w; ++xi) {
                const int worldX = centerX - radius + xi * step;
                const int worldZ = centerZ - radius + zi * step;
                const float hh = (float)ComputeColumn(worldX, worldZ).surfaceY;
                int idx = (int)std::floor(((hh - minH) / heightRange) * 255.0f + 0.5f);
                idx = SafeClamp(idx, 0, 255);
                bins[(size_t)idx]++;
            }
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
        out.artifactDensity = 0.02f;
        return out;
    }

}}