#pragma once

#include <glm/glm.hpp>

namespace Engine {
    class QualityManager {
    public:
        static void Init();
        static void Update(float fps, const glm::vec3& camPos);
        static int GetViewDistance();
        static int GetLodLevel();
        static int GetUploadPerFrame();
    };
}
