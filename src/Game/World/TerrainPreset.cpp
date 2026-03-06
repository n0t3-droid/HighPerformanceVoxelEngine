#include "TerrainPreset.h"
#include "Generation.h"

namespace Game { namespace World {

    // LowSpecMode: Reduziert Detail und Noise-Frequenzen für schwache Hardware
    const TerrainPreset g_TerrainPresets[8] = {
        {"Plateau Metropolis", "Riesige glatte Plateaus – perfekt fuer endlose moderne Staedte", "#A0D8FF", 52.0f, 38.0f, 4.2f, 12.0f, 85.0f, 0.18f, 0.00018f, 0.00045f, 0.00014f, 2, 1, false},
        {"Majestic Peaks", "Elegante Berge die harmonisch mit Plateaus verschmelzen", "#88CCAA", 48.0f, 18.0f, 12.5f, 68.0f, 295.0f, 0.25f, 0.00022f, 0.00068f, 0.00021f, 3, 1, false},
        {"Coastal Paradise", "Breite Straende, Klippen und ruhige Meere", "#66BBFF", 38.0f, 22.0f, 8.5f, 55.0f, 65.0f, 0.42f, 0.00021f, 0.00055f, 0.00013f, 4, 4, false},
        {"Canyon Realm", "Tiefe, mystische Canyons mit natuerlichen Bruecken", "#DDAACC", 62.0f, 12.0f, 18.0f, 92.0f, 145.0f, 0.58f, 0.00032f, 0.00075f, 0.00026f, 3, 1, false},
        {"Highland Fantasy", "Sanfte Huegel und hohe verzauberte Plateaus", "#99EEBB", 55.0f, 28.0f, 14.0f, 38.0f, 185.0f, 0.29f, 0.00024f, 0.00065f, 0.00015f, 2, 1, false},
        {"Desert Oasis", "Weite Sandplateaus mit seltenen gruennen Oasen", "#FFEEAA", 48.0f, 15.0f, 5.5f, 22.0f, 75.0f, 0.15f, 0.00025f, 0.00052f, 0.00013f, 4, 1, false},
        {"Archipelago Dream", "Viele kleine Inseln mit perfekten Straenden", "#77DDFF", 35.0f, 25.0f, 9.0f, 48.0f, 55.0f, 0.78f, 0.00018f, 0.00045f, 0.00021f, 4, 4, false},
        {"Mystic Valleys", "Tiefe, friedliche Taeler mit sanften Huegeln", "#AACCFF", 58.0f, 19.0f, 11.0f, 35.0f, 125.0f, 0.38f, 0.00019f, 0.00062f, 0.00018f, 2, 1, false}
    };

    int g_CurrentPresetIndex = 0;

    void ApplyPresetToNoise(bool lowSpecMode) {
        // Wird in Generation.cpp konkret auf die FastNoise-Instanzen angewendet.
        // lowSpecMode: Frequenzen/Amp halbieren, Details reduzieren
        if (lowSpecMode) {
            for (int i = 0; i < 8; ++i) {
                TerrainPreset& p = const_cast<TerrainPreset&>(g_TerrainPresets[i]);
                p.continentFreq *= 0.5f;
                p.plateauFreq *= 0.5f;
                p.riverFreq *= 0.5f;
                p.plateauAmp *= 0.6f;
            }
        }
    }

}}