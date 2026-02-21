#pragma once

#include <string>
#include <glm/glm.hpp>
#include <cstdint>

namespace Game { namespace World {

    struct TerrainPreset {
        std::string name;
        std::string description;
        std::string previewHex; // Farbe für Preview-Karte im Menu
        float baseHeight;
        float plateauAmp;
        float slopeAmp;
        float cliffAmp;
        float mountainAmp;
        float riverDensity; // 0.0 = keine Flüsse, 1.0 = viele
        float continentFreq;
        float plateauFreq;
        float riverFreq;
        uint8_t defaultTopBlock;
        uint8_t riverTopBlock;
    };

    extern const TerrainPreset g_TerrainPresets[8];
    extern int g_CurrentPresetIndex;

    void ApplyPresetToNoise();

}}