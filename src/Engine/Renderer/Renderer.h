#pragma once

#include <glm/glm.hpp>

namespace Engine {
    class Renderer {
    public:
        static void Init();
        static void Shutdown();

        static void Clear();
        static void SetClearColor(const glm::vec4& color);
        
        static void BeginFrame();
        static void EndFrame();
    };
}
