#include "Engine/Core/QualityManager.h"

#include <algorithm>

namespace Engine {
    static int s_ViewDist = 168;
    static int s_MaxView = 224;
    static int s_LodLevel = 0;

    void QualityManager::Init() {
        s_ViewDist = 168;
        s_LodLevel = 0;
    }

    void QualityManager::Update(float fps, const glm::vec3& camPos) {
        (void)camPos;
        if (fps > 110.0f) {
            s_ViewDist = s_MaxView;
        } else if (fps > 80.0f) {
            s_ViewDist = 196;
        } else if (fps > 55.0f) {
            s_ViewDist = 168;
        } else {
            s_ViewDist = 112;
        }

        s_LodLevel = (fps < 45.0f) ? 1 : 0;
    }

    int QualityManager::GetViewDistance() { return s_ViewDist; }
    int QualityManager::GetLodLevel() { return s_LodLevel; }
    int QualityManager::GetUploadPerFrame() { return 42; }
}
