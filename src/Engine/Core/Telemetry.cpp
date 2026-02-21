#include "Engine/Core/Telemetry.h"

#include "Engine/Core/Input.h"
#include "Engine/Core/RunLog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace Engine {

    struct ReplayEvent {
        double t = 0.0;
        char type = 0; // K/M/D/S
        int a = 0;
        int b = 0;
        int c = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    static std::mutex s_Mutex;
    static std::ofstream s_Out;
    static std::ofstream s_LiveOut;

    static bool s_Recording = false;
    static bool s_Replaying = false;
    static bool s_Live = false;

    static std::vector<ReplayEvent> s_Replay;
    static std::size_t s_ReplayIndex = 0;

    static int s_FlushEvery = 8;
    static int s_PendingLines = 0;

    static double s_LiveInterval = 0.2;
    static double s_LastLiveTime = -1.0;

    static EliteTelemetrySnapshot s_EliteSnapshot;

    static int GetEnvIntOr(const char* name, int fallback) {
        const char* v = std::getenv(name);
        if (!v || !*v) return fallback;
        try {
            return std::stoi(v);
        } catch (...) {
            return fallback;
        }
    }

    static double GetEnvDoubleOr(const char* name, double fallback) {
        const char* v = std::getenv(name);
        if (!v || !*v) return fallback;
        try {
            return std::stod(v);
        } catch (...) {
            return fallback;
        }
    }

    static bool GetEnvBool(const char* name, bool fallback) {
        const char* v = std::getenv(name);
        if (!v || !*v) return fallback;
        return (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T');
    }

    static std::string GetEnvStr(const char* name, const char* fallback) {
        const char* v = std::getenv(name);
        if (!v || !*v) return std::string(fallback);
        return std::string(v);
    }

    static std::string Trim(std::string s) {
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        std::size_t i = 0;
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        return s.substr(i);
    }

    void Telemetry::Init() {
        std::scoped_lock lock(s_Mutex);

        s_Recording = GetEnvBool("HVE_TRACE", false);
        const std::string recordFile = GetEnvStr("HVE_TRACE_FILE", "hve_trace.log");
        const std::string replayFile = GetEnvStr("HVE_REPLAY_FILE", "");

        s_Live = GetEnvBool("HVE_LIVE", false);
        const std::string liveFile = GetEnvStr("HVE_LIVE_FILE", "hve_live.log");
        const double liveHz = GetEnvDoubleOr("HVE_LIVE_HZ", 5.0);
        s_LiveInterval = (liveHz > 0.0) ? (1.0 / liveHz) : 0.2;

        s_FlushEvery = std::max(1, GetEnvIntOr("HVE_TRACE_FLUSH_EVERY", 8));

        if (!replayFile.empty()) {
            std::ifstream in(replayFile);
            if (!in) {
                RunLog::Error("Replay file not found: " + replayFile);
            } else {
                s_Replaying = true;
                Input::SetReplayMode(true);

                std::string line;
                while (std::getline(in, line)) {
                    line = Trim(line);
                    if (line.empty()) continue;
                    if (line[0] == '#') continue;

                    std::istringstream ss(line);
                    ReplayEvent e;
                    ss >> e.type;
                    if (!ss) continue;

                    if (e.type == 'K') {
                        ss >> e.t >> e.a >> e.b >> e.c; // key action mods
                    } else if (e.type == 'M') {
                        ss >> e.t >> e.a >> e.b >> e.c; // button action mods
                    } else if (e.type == 'D') {
                        ss >> e.t >> e.x >> e.y; // dx dy
                    } else if (e.type == 'S') {
                        ss >> e.t >> e.y; // scrollY
                    } else {
                        continue; // ignore other lines
                    }

                    if (ss) s_Replay.push_back(e);
                }

                std::sort(s_Replay.begin(), s_Replay.end(), [](const ReplayEvent& a, const ReplayEvent& b) {
                    return a.t < b.t;
                });

                RunLog::Info("Replay enabled: " + replayFile + " (events=" + std::to_string(s_Replay.size()) + ")");
            }
        }

        if (s_Recording) {
            s_Out.open(recordFile, std::ios::out | std::ios::trunc);
            if (!s_Out) {
                s_Recording = false;
                RunLog::Error("Failed to open trace file: " + recordFile);
            } else {
                s_Out << "# HVE TRACE v1\n";
                s_Out << "# Types: K t key action mods | M t button action mods | D t dx dy | S t scrollY\n";
                s_Out << "# Gameplay: SEL t id | BRK t x y z oldId | PLC t x y z newId | CAM t px py pz yaw pitch\n";
                s_Out.flush();
                RunLog::Info("Trace recording enabled: " + recordFile);
            }
        }

        if (s_Live) {
            s_LiveOut.open(liveFile, std::ios::out | std::ios::trunc);
            if (!s_LiveOut) {
                s_Live = false;
                RunLog::Error("Failed to open live file: " + liveFile);
            } else {
                s_LiveOut << "# HVE LIVE v1\n";
                s_LiveOut << "# LIVE t px py pz yaw pitch chunks tris draws fps\n";
                s_LiveOut.flush();
                RunLog::Info("Live stream enabled: " + liveFile);
            }
        }

    }

    void Telemetry::Shutdown() {
        std::scoped_lock lock(s_Mutex);
        if (s_Out.is_open()) {
            s_Out << "END\n";
            s_Out.flush();
            s_Out.close();
        }
        if (s_LiveOut.is_open()) {
            s_LiveOut << "END\n";
            s_LiveOut.flush();
            s_LiveOut.close();
        }
        s_Recording = false;
        s_Live = false;
    }

    bool Telemetry::IsRecording() { return s_Recording; }
    bool Telemetry::IsReplaying() { return s_Replaying; }

    void Telemetry::WriteLine(const std::string& line) {
        if (!s_Out.is_open()) return;
        s_Out << line << "\n";
        s_PendingLines++;
        if (s_PendingLines >= s_FlushEvery) {
            s_Out.flush();
            s_PendingLines = 0;
        }
    }

    void Telemetry::RecordKey(double t, int key, int action, int mods) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "K " << t << ' ' << key << ' ' << action << ' ' << mods;
        WriteLine(ss.str());
    }

    void Telemetry::RecordMouseButton(double t, int button, int action, int mods) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "M " << t << ' ' << button << ' ' << action << ' ' << mods;
        WriteLine(ss.str());
    }

    void Telemetry::RecordMouseDelta(double t, float dx, float dy) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "D " << t << ' ' << dx << ' ' << dy;
        WriteLine(ss.str());
    }

    void Telemetry::RecordScroll(double t, float dy) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "S " << t << ' ' << dy;
        WriteLine(ss.str());
    }

    void Telemetry::RecordSelectBlock(double t, std::uint8_t blockId) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "SEL " << t << ' ' << (int)blockId;
        WriteLine(ss.str());
    }

    void Telemetry::RecordBlockBreak(double t, const glm::ivec3& worldPos, std::uint8_t oldId) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "BRK " << t << ' ' << worldPos.x << ' ' << worldPos.y << ' ' << worldPos.z << ' ' << (int)oldId;
        WriteLine(ss.str());
    }

    void Telemetry::RecordBlockPlace(double t, const glm::ivec3& worldPos, std::uint8_t newId) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "PLC " << t << ' ' << worldPos.x << ' ' << worldPos.y << ' ' << worldPos.z << ' ' << (int)newId;
        WriteLine(ss.str());
    }

    void Telemetry::RecordCamera(double t, const glm::vec3& pos, float yaw, float pitch) {
        if (!s_Recording) return;
        std::scoped_lock lock(s_Mutex);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(6);
        ss << "CAM " << t << ' ' << pos.x << ' ' << pos.y << ' ' << pos.z << ' ' << yaw << ' ' << pitch;
        WriteLine(ss.str());
    }

    void Telemetry::RecordLiveState(double t, const glm::vec3& pos, float yaw, float pitch,
                                    int chunks, int tris, int draws, float fps) {
        if (!s_Live || !s_LiveOut.is_open()) return;
        if (s_LastLiveTime >= 0.0 && (t - s_LastLiveTime) < s_LiveInterval) return;

        std::scoped_lock lock(s_Mutex);
        s_LastLiveTime = t;

        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(4);
        ss << "LIVE " << t << ' '
           << pos.x << ' ' << pos.y << ' ' << pos.z << ' '
           << yaw << ' ' << pitch << ' '
           << chunks << ' ' << tris << ' ' << draws << ' ' << fps;
        s_LiveOut << ss.str() << "\n";
        s_LiveOut.flush();
    }

    void Telemetry::PumpReplay(double t) {
        if (!s_Replaying) return;

        // Apply all events up to time t
        while (s_ReplayIndex < s_Replay.size() && s_Replay[s_ReplayIndex].t <= t) {
            const ReplayEvent& e = s_Replay[s_ReplayIndex];
            if (e.type == 'K') {
                const bool down = (e.b != 0); // GLFW_PRESS=1, GLFW_RELEASE=0
                Input::ReplayInjectKey(e.a, down);
            } else if (e.type == 'M') {
                const bool down = (e.b != 0);
                Input::ReplayInjectMouseButton(e.a, down);
            } else if (e.type == 'D') {
                Input::ReplayInjectMouseDelta(e.x, e.y);
            } else if (e.type == 'S') {
                Input::ReplayInjectScroll(e.y);
            }
            s_ReplayIndex++;
        }
    }

    void Telemetry::UpdateEliteMetrics(const EliteTelemetryInput& rawInput) {
        std::scoped_lock lock(s_Mutex);

        EliteTelemetryInput input = rawInput;
        input.fpsTarget = std::max(1.0f, input.fpsTarget);
        input.viewDistanceChunks = std::clamp(input.viewDistanceChunks, 1.0f, 256.0f);
        input.mainThreadBlockPct = std::clamp(input.mainThreadBlockPct, 0.0f, 1.0f);
        input.gpuStallPct = std::clamp(input.gpuStallPct, 0.0f, 1.0f);
        input.visualArtifactDensity = std::max(0.001f, input.visualArtifactDensity);
        input.totalActiveVRAMMB = std::max(0.0f, input.totalActiveVRAMMB);
        input.cpuRAMPeakGB = std::max(0.0f, input.cpuRAMPeakGB);
        input.cacheMissRate = std::clamp(input.cacheMissRate, 0.0f, 1.0f);
        input.memoryUsagePct = std::clamp(input.memoryUsagePct, 0.0f, 1.0f);
        input.loadTimeFactor = std::clamp(input.loadTimeFactor, 0.0f, 1.0f);
        input.buildStability = std::clamp(input.buildStability, 0.0f, 1.0f);
        input.activeChunks = std::max(0, input.activeChunks);
        input.meshPoolMB = std::max(0.0f, input.meshPoolMB);
        input.availableRamGB = std::max(0.1f, input.availableRamGB);
        input.biomeEntropy = std::max(0.0f, input.biomeEntropy);

        const float targetFrameMs = 1000.0f / input.fpsTarget;
        const float spikeFactor = std::clamp(1.0f - (input.maxSpikeMs / targetFrameMs), 0.0f, 1.0f);
        const float viewFactor = std::clamp(input.viewDistanceChunks / 128.0f, 0.0f, 2.0f);
        const float mainThreadFactor = 1.0f - input.mainThreadBlockPct;
        const float gpuFactor = 1.0f - input.gpuStallPct;

        const float smoothness = input.fpsStable * spikeFactor * viewFactor * mainThreadFactor * gpuFactor;

        const float realismNumerator =
            input.heightEntropy * 0.30f +
            input.biomeVariance * 0.25f +
            input.erosionDetail * 0.25f +
            input.pbrMetallicRoughnessVariation * 0.20f;
        const float realism = realismNumerator / input.visualArtifactDensity;

        const float memoryEff =
            (input.totalActiveVRAMMB / 496.0f) +
            (input.cpuRAMPeakGB / 32.0f) +
            (input.cacheMissRate * 10.0f);

        float cacheEff = 1.0f - input.cacheMissRate;
        if (input.totalAccesses > 0) {
            const double misses = (double)input.l1Misses + (double)input.l2Misses;
            cacheEff = (float)std::clamp(1.0 - (misses / (double)input.totalAccesses), 0.0, 1.0);
        }

        const float tspSpikeFactor = std::clamp(1.0f - (input.maxSpikeMs / 16.67f), 0.0f, 1.0f);
        const float toasterStability =
            input.fpsStable *
            tspSpikeFactor *
            (1.0f - input.memoryUsagePct) *
            input.loadTimeFactor *
            input.buildStability;

        const float toasterRealism =
            (input.erosionDetail + input.pbrMetallicRoughnessVariation + input.biomeEntropy) /
            (1.0f + std::max(0.0f, input.visualArtifactDensity));

        const float memoryGuaranteeBytes =
            (float)input.activeChunks * 3.8f * 1024.0f +
            (input.meshPoolMB * 0.42f * 1024.0f * 1024.0f);
        const float memoryGuaranteeLimit = input.availableRamGB * 0.55f * 1024.0f * 1024.0f * 1024.0f;

        s_EliteSnapshot.input = input;
        s_EliteSnapshot.smoothnessIndex = smoothness;
        s_EliteSnapshot.realismScore = realism;
        s_EliteSnapshot.memoryEfficiency = memoryEff;
        s_EliteSnapshot.cacheEfficiency = cacheEff;
        s_EliteSnapshot.toasterStabilityIndex = toasterStability;
        s_EliteSnapshot.toasterRealismScore = toasterRealism;
        s_EliteSnapshot.memoryGuarantee = memoryGuaranteeBytes;
        s_EliteSnapshot.memoryGuaranteeLimit = memoryGuaranteeLimit;
        s_EliteSnapshot.smoothnessPass = smoothness >= 245.0f;
        s_EliteSnapshot.realismPass = realism >= 9.8f;
        s_EliteSnapshot.memoryPass = memoryEff <= 0.58f;
        s_EliteSnapshot.cachePass = cacheEff > 0.94f;
        s_EliteSnapshot.toasterStabilityPass = toasterStability >= 95.0f;
        s_EliteSnapshot.toasterRealismPass = toasterRealism >= 9.7f;
        s_EliteSnapshot.memoryGuaranteePass = memoryGuaranteeBytes <= memoryGuaranteeLimit;
    }

    EliteTelemetrySnapshot Telemetry::GetEliteMetrics() {
        std::scoped_lock lock(s_Mutex);
        return s_EliteSnapshot;
    }

}
