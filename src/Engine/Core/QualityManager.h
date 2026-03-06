// ============================================================================
//  INVENTION: Adaptive Quality Controller v2
//
//  Replaces the simple 4-tier FPS switch with a smooth, hardware-aware
//  system that gradually ramps quality up and down.
//
//  Key mechanisms:
//  - Exponential moving average (EMA) of FPS for noise-resistant decisions
//  - Hysteresis bands prevent flickering at tier boundaries
//  - View distance ramps continuously (lerp, not switch) for seamless feel
//  - Upload budget scales with available frame headroom
//  - Emergency mode: sustained low FPS triggers aggressive quality drop
//  - Recovery is 3x slower than degradation (drop fast, recover slowly)
//  - Hardware-aware initialization via HardwareDetector profile
//
//  Low-PC path: UltraLow and Toaster-class hardware start at reduced
//  defaults and have tighter ceilings so they never attempt settings
//  that would cause stalls.
// ============================================================================
#pragma once

#include <glm/glm.hpp>

namespace Engine {

    struct HardwareProfile;

    class QualityManager {
    public:
        /// Initialize with hardware profile for tailored defaults.
        static void Init();
        static void InitWithProfile(const HardwareProfile& profile);

        /// Call once per frame with current measured FPS.
        static void Update(float fps, const glm::vec3& camPos);

        /// Current smoothed view distance in blocks.
        static int  GetViewDistance();
        /// LOD level: 0 = full, 1 = reduced, 2 = minimal.
        static int  GetLodLevel();
        /// How many chunk GPU uploads to allow this frame.
        static int  GetUploadPerFrame();
        /// True when system is in emergency low-FPS mode.
        static bool IsEmergencyMode();
        /// Current smoothed FPS estimate (for debug HUD).
        static float GetSmoothedFps();
    };

}
