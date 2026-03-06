// ============================================================================
//  Adaptive Quality Controller v2 — Implementation
// ============================================================================
#include "Engine/Core/QualityManager.h"
#include "Engine/Core/HardwareDetector.h"

#include <algorithm>
#include <cmath>

namespace Engine {

    // -------- State --------
    static float  s_EmaFps         = 60.0f;  // Exponential-moving-average FPS
    static float  s_ViewDistF      = 168.0f; // Continuous view distance (float for smooth lerp)
    static int    s_LodLevel       = 0;
    static int    s_UploadBudget   = 42;
    static bool   s_Emergency      = false;
    static int    s_EmergencyFrames = 0;      // Consecutive frames below critical

    // -------- Limits (adjusted by hardware) --------
    static float  s_MinView        = 80.0f;
    static float  s_MaxView        = 224.0f;
    static float  s_TargetFps      = 60.0f;
    static int    s_MaxUpload      = 48;
    static int    s_MinUpload      = 8;

    // -------- Tuning constants --------
    static constexpr float EMA_ALPHA_DOWN  = 0.15f;  // Fast reaction to drops
    static constexpr float EMA_ALPHA_UP    = 0.05f;  // Slow recovery
    static constexpr float RAMP_SPEED_DOWN = 4.0f;   // Blocks/frame drop speed
    static constexpr float RAMP_SPEED_UP   = 1.2f;   // Blocks/frame recovery speed
    static constexpr float EMERGENCY_FPS   = 28.0f;  // Critical threshold
    static constexpr int   EMERGENCY_HOLD  = 90;     // Frames to hold before recovery

    // ================================================================
    void QualityManager::Init() {
        s_EmaFps = 60.0f;
        s_ViewDistF = 168.0f;
        s_LodLevel = 0;
        s_UploadBudget = 42;
        s_Emergency = false;
        s_EmergencyFrames = 0;
        s_MinView = 80.0f;
        s_MaxView = 224.0f;
        s_TargetFps = 60.0f;
        s_MaxUpload = 48;
        s_MinUpload = 8;
    }

    void QualityManager::InitWithProfile(const HardwareProfile& profile) {
        Init();
        // Tailor limits to detected hardware.
        if (profile.IsToasterClass()) {
            s_MaxView   = 128.0f;
            s_MinView   = 48.0f;
            s_ViewDistF = 96.0f;
            s_MaxUpload = 24;
            s_TargetFps = 45.0f;
        } else if (profile.gpuTier == GpuTier::UltraLow) {
            s_MaxView   = 160.0f;
            s_MinView   = 64.0f;
            s_ViewDistF = 128.0f;
            s_MaxUpload = 32;
            s_TargetFps = 50.0f;
        } else if (profile.gpuTier == GpuTier::Low || profile.IsVegaApuClass()) {
            s_MaxView   = 192.0f;
            s_MinView   = 80.0f;
            s_ViewDistF = 168.0f;
            s_MaxUpload = 42;
        } else if (profile.gpuTier == GpuTier::High) {
            s_MaxView   = 320.0f;
            s_MinView   = 112.0f;
            s_ViewDistF = 224.0f;
            s_MaxUpload = 64;
        }
    }

    // ================================================================
    void QualityManager::Update(float fps, const glm::vec3& camPos) {
        (void)camPos;

        // ---- 1. Smooth FPS with asymmetric EMA (drop fast, recover slowly) ----
        const float alpha = (fps < s_EmaFps) ? EMA_ALPHA_DOWN : EMA_ALPHA_UP;
        s_EmaFps = s_EmaFps + alpha * (fps - s_EmaFps);
        s_EmaFps = std::max(1.0f, s_EmaFps);

        // ---- 2. Emergency mode detection ----
        if (s_EmaFps < EMERGENCY_FPS) {
            s_EmergencyFrames++;
            if (s_EmergencyFrames > 10) {
                s_Emergency = true;
            }
        } else if (s_Emergency) {
            // Require sustained good FPS before exiting emergency.
            if (s_EmaFps > s_TargetFps * 0.85f) {
                s_EmergencyFrames--;
                if (s_EmergencyFrames <= -EMERGENCY_HOLD) {
                    s_Emergency = false;
                    s_EmergencyFrames = 0;
                }
            }
        } else {
            s_EmergencyFrames = std::max(0, s_EmergencyFrames - 1);
        }

        // ---- 3. Compute target view distance from FPS headroom ----
        float targetView;
        if (s_Emergency) {
            targetView = s_MinView;
        } else {
            // Linear interpolation: at (targetFps - 15) → minView, at (targetFps + 40) → maxView.
            const float lo = s_TargetFps - 15.0f;
            const float hi = s_TargetFps + 40.0f;
            float t = (s_EmaFps - lo) / (hi - lo);
            t = std::clamp(t, 0.0f, 1.0f);
            targetView = s_MinView + t * (s_MaxView - s_MinView);
        }

        // ---- 4. Smooth ramp towards target (asymmetric: fast down, slow up) ----
        const float diff = targetView - s_ViewDistF;
        if (diff < 0.0f) {
            s_ViewDistF += std::max(diff, -RAMP_SPEED_DOWN); // Drop fast
        } else {
            s_ViewDistF += std::min(diff, RAMP_SPEED_UP);   // Recover slowly
        }
        s_ViewDistF = std::clamp(s_ViewDistF, s_MinView, s_MaxView);

        // ---- 5. LOD level (3-tier) ----
        if (s_Emergency || s_EmaFps < s_TargetFps * 0.60f) {
            s_LodLevel = 2;
        } else if (s_EmaFps < s_TargetFps * 0.80f) {
            s_LodLevel = 1;
        } else {
            s_LodLevel = 0;
        }

        // ---- 6. Dynamic upload budget ----
        // Scale uploads proportionally to FPS headroom.
        const float headroom = (s_EmaFps - s_TargetFps * 0.7f) / (s_TargetFps * 0.4f);
        const float t2 = std::clamp(headroom, 0.0f, 1.0f);
        s_UploadBudget = (int)std::round((float)s_MinUpload + t2 * (float)(s_MaxUpload - s_MinUpload));
        if (s_Emergency) s_UploadBudget = s_MinUpload;
    }

    // ================================================================
    int   QualityManager::GetViewDistance()   { return (int)std::round(s_ViewDistF); }
    int   QualityManager::GetLodLevel()       { return s_LodLevel; }
    int   QualityManager::GetUploadPerFrame() { return s_UploadBudget; }
    bool  QualityManager::IsEmergencyMode()   { return s_Emergency; }
    float QualityManager::GetSmoothedFps()    { return s_EmaFps; }

}
