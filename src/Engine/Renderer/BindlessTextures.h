#pragma once

#include <cstdint>
#include <glad/glad.h>

namespace Engine::RendererUtil {

    // Minimal helper for GL_ARB_bindless_texture via runtime-loaded entry points.
    // Safe to call even when unsupported; functions will no-op and return false.
    struct BindlessTextures {
        static bool Init();
        static bool IsSupported();

        static std::uint64_t GetTextureHandle(GLuint texture);
        static std::uint64_t GetTextureSamplerHandle(GLuint texture, GLuint sampler);
        static bool IsTextureHandleResident(std::uint64_t handle);
        static void MakeTextureHandleResident(std::uint64_t handle);
        static void MakeTextureHandleNonResident(std::uint64_t handle);

        static void SetUniformHandle(GLuint program, GLint location, std::uint64_t handle);
    };

} // namespace Engine::RendererUtil
