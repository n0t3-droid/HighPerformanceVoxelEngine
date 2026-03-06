#pragma once

#include <cstdint>
#include <vector>

namespace Game { namespace World {

    struct HydrologyMaps {
        int width = 0;
        int height = 0;
        std::vector<float> filledHeight;
        std::vector<int8_t> flowDirD8;
        std::vector<float> flowAccum;
    };

    class Hydrology {
    public:
        static bool PriorityFloodFill(const std::vector<float>& heightIn, int width, int height, std::vector<float>& outFilled);
        static bool ComputeFlowDirectionD8(const std::vector<float>& heightField, int width, int height, std::vector<int8_t>& outDirD8);
        static bool ComputeFlowAccumulation(const std::vector<int8_t>& dirD8, const std::vector<float>& heightField, int width, int height, std::vector<float>& outAccum);
        static bool BuildHydrologyMaps(const std::vector<float>& heightIn, int width, int height, HydrologyMaps& outMaps);
    };

}}