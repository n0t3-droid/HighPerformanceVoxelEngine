#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace Engine {


    struct EliteTelemetryInput {
        float fpsStable = 0.0f;
        float maxSpikeMs = 0.0f;
        float fpsTarget = 144.0f;
        float viewDistanceChunks = 24.0f;
        float mainThreadBlockPct = 0.0f; // 0..1
        float gpuStallPct = 0.0f;        // 0..1

        float heightEntropy = 0.0f;
        float biomeVariance = 0.0f;
        float erosionDetail = 0.0f;
        float pbrMetallicRoughnessVariation = 0.0f;
        float visualArtifactDensity = 1.0f;

        float totalActiveVRAMMB = 0.0f;
        float cpuRAMPeakGB = 0.0f;
        float cacheMissRate = 0.0f; // 0..1

        float memoryUsagePct = 0.0f;    // 0..1
        float loadTimeFactor = 1.0f;    // 0..1
        float buildStability = 1.0f;    // 0..1

        int activeChunks = 0;
        float meshPoolMB = 0.0f;
        float availableRamGB = 8.0f;

        float biomeEntropy = 0.0f;

        std::uint64_t l1Misses = 0;
        std::uint64_t l2Misses = 0;
        std::uint64_t totalAccesses = 0;
    };

    struct EliteTelemetrySnapshot {
        float smoothnessIndex = 0.0f;  // Target >= 245
        float realismScore = 0.0f;     // Target >= 9.8
        float memoryEfficiency = 0.0f; // Target <= 0.58
        float cacheEfficiency = 0.0f;  // Target > 0.94
        float toasterStabilityIndex = 0.0f; // Target >= 95
        float toasterRealismScore = 0.0f;   // Target >= 9.7
        float memoryGuarantee = 0.0f;       // bytes estimate
        float memoryGuaranteeLimit = 0.0f;  // bytes threshold

        bool smoothnessPass = false;
        bool realismPass = false;
        bool memoryPass = false;
        bool cachePass = false;
        bool toasterStabilityPass = false;
        bool toasterRealismPass = false;
        bool memoryGuaranteePass = false;

        EliteTelemetryInput input{};
    };

    class Telemetry {
    public:
        static void Init();
        static void Shutdown();

        static bool IsRecording();
        static bool IsReplaying();

        // Recording API (called from callbacks / gameplay code)
        static void RecordKey(double t, int key, int action, int mods);
        static void RecordMouseButton(double t, int button, int action, int mods);
        static void RecordMouseDelta(double t, float dx, float dy);
        static void RecordScroll(double t, float dy);

        static void RecordSelectBlock(double t, std::uint8_t blockId);
        static void RecordBlockBreak(double t, const glm::ivec3& worldPos, std::uint8_t oldId);
        static void RecordBlockPlace(double t, const glm::ivec3& worldPos, std::uint8_t newId);
        static void RecordCamera(double t, const glm::vec3& pos, float yaw, float pitch);

        // Optional live state stream (for tailing while running).
        static void RecordLiveState(double t, const glm::vec3& pos, float yaw, float pitch,
                        int chunks, int tris, int draws, float fps);

        static void UpdateEliteMetrics(const EliteTelemetryInput& input);
        static EliteTelemetrySnapshot GetEliteMetrics();

        // Replay API (call once per frame after Input::Update and before reading input)
        static void PumpReplay(double t);

    private:
        static void WriteLine(const std::string& line);
    };
}
