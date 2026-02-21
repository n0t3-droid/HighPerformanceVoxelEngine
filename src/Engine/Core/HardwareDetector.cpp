#include "Engine/Core/HardwareDetector.h"

#include <algorithm>
#include <cctype>
#include <thread>

#include <glad/glad.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Engine {

    static std::string LowerCopy(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        return text;
    }

    static GpuTier DetectGpuTier(const std::string& vendor, const std::string& renderer) {
        const std::string joined = LowerCopy(vendor + " " + renderer);

        if (joined.find("intel") != std::string::npos &&
            (joined.find("hd") != std::string::npos || joined.find("uhd") != std::string::npos)) {
            return GpuTier::UltraLow;
        }

        if (joined.find("vega 8") != std::string::npos ||
            joined.find("vega") != std::string::npos ||
            joined.find("radeon") != std::string::npos) {
            return GpuTier::Low;
        }

        if (joined.find("rtx") != std::string::npos ||
            joined.find("rx 7") != std::string::npos ||
            joined.find("rx 8") != std::string::npos) {
            return GpuTier::High;
        }

        if (joined.find("gtx") != std::string::npos ||
            joined.find("rx") != std::string::npos) {
            return GpuTier::Mid;
        }

        return GpuTier::Low;
    }

    HardwareProfile HardwareDetector::Detect() {
        HardwareProfile p;

        p.logicalCores = std::max(1u, std::thread::hardware_concurrency());
        p.physicalCores = std::max(1u, p.logicalCores / 2u);

#ifdef _WIN32
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem)) {
            p.systemRamGB = (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        }
#else
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0) {
            p.systemRamGB = ((double)pages * (double)pageSize) / (1024.0 * 1024.0 * 1024.0);
        }
#endif

        const GLubyte* vendorPtr = glGetString(GL_VENDOR);
        const GLubyte* rendererPtr = glGetString(GL_RENDERER);
        p.gpuVendor = vendorPtr ? reinterpret_cast<const char*>(vendorPtr) : "Unknown";
        p.gpuRenderer = rendererPtr ? reinterpret_cast<const char*>(rendererPtr) : "Unknown";
        p.gpuTier = DetectGpuTier(p.gpuVendor, p.gpuRenderer);

        return p;
    }

}
