#pragma once

#include <string>

namespace Engine {

    enum class GpuTier {
        UltraLow = 0,
        Low = 1,
        Mid = 2,
        High = 3
    };

    struct HardwareProfile {
        unsigned int logicalCores = 1;
        unsigned int physicalCores = 1;
        double systemRamGB = 4.0;
        std::string gpuVendor;
        std::string gpuRenderer;
        GpuTier gpuTier = GpuTier::Low;

        bool IsToasterClass() const {
            return systemRamGB < 12.0 || logicalCores <= 4 || gpuTier == GpuTier::UltraLow;
        }
    };

    class HardwareDetector {
    public:
        static HardwareProfile Detect();
    };

}
