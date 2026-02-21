#include "Engine/Renderer/BindlessTextures.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <string>
#include <cstdlib>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

namespace Engine::RendererUtil {

    static bool ShouldLogBindless() {
        const char* v = std::getenv("HVE_LOG_BINDLESS");
        return v && *v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T');
    }

    typedef GLuint64(APIENTRY* PFNGLGETTEXTUREHANDLEARBPROC)(GLuint texture);
    typedef GLuint64(APIENTRY* PFNGLGETTEXTURESAMPLERHANDLEARBPROC)(GLuint texture, GLuint sampler);
    typedef void (APIENTRY* PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)(GLuint64 handle);
    typedef void (APIENTRY* PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)(GLuint64 handle);
    typedef GLboolean(APIENTRY* PFNGLISTEXTUREHANDLERESIDENTARBPROC)(GLuint64 handle);
    typedef void (APIENTRY* PFNGLUNIFORMHANDLEUI64ARBPROC)(GLint location, GLuint64 value);
    typedef void (APIENTRY* PFNGLPROGRAMUNIFORMHANDLEUI64ARBPROC)(GLuint program, GLint location, GLuint64 value);

    static PFNGLGETTEXTUREHANDLEARBPROC s_glGetTextureHandleARB = nullptr;
    static PFNGLGETTEXTURESAMPLERHANDLEARBPROC s_glGetTextureSamplerHandleARB = nullptr;
    static PFNGLMAKETEXTUREHANDLERESIDENTARBPROC s_glMakeTextureHandleResidentARB = nullptr;
    static PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC s_glMakeTextureHandleNonResidentARB = nullptr;
    static PFNGLISTEXTUREHANDLERESIDENTARBPROC s_glIsTextureHandleResidentARB = nullptr;
    static PFNGLUNIFORMHANDLEUI64ARBPROC s_glUniformHandleui64ARB = nullptr;
    static PFNGLPROGRAMUNIFORMHANDLEUI64ARBPROC s_glProgramUniformHandleui64ARB = nullptr;

    static bool s_Inited = false;
    static bool s_Supported = false;

    static bool HasExtension(const char* extName) {
        if (!extName) return false;
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i) {
            const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (ext && std::string(ext) == extName) return true;
        }
        return false;
    }

    bool BindlessTextures::Init() {
        if (s_Inited) return s_Supported;
        s_Inited = true;

        s_glGetTextureHandleARB = (PFNGLGETTEXTUREHANDLEARBPROC)glfwGetProcAddress("glGetTextureHandleARB");
        s_glGetTextureSamplerHandleARB = (PFNGLGETTEXTURESAMPLERHANDLEARBPROC)glfwGetProcAddress("glGetTextureSamplerHandleARB");
        s_glMakeTextureHandleResidentARB = (PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)glfwGetProcAddress("glMakeTextureHandleResidentARB");
        s_glMakeTextureHandleNonResidentARB = (PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)glfwGetProcAddress("glMakeTextureHandleNonResidentARB");
        s_glIsTextureHandleResidentARB = (PFNGLISTEXTUREHANDLERESIDENTARBPROC)glfwGetProcAddress("glIsTextureHandleResidentARB");
        s_glUniformHandleui64ARB = (PFNGLUNIFORMHANDLEUI64ARBPROC)glfwGetProcAddress("glUniformHandleui64ARB");
        s_glProgramUniformHandleui64ARB = (PFNGLPROGRAMUNIFORMHANDLEUI64ARBPROC)glfwGetProcAddress("glProgramUniformHandleui64ARB");

        const bool extOk = HasExtension("GL_ARB_bindless_texture");

        const bool hasSetter = (s_glProgramUniformHandleui64ARB != nullptr) || (s_glUniformHandleui64ARB != nullptr);
        s_Supported = extOk && (s_glGetTextureHandleARB && s_glMakeTextureHandleResidentARB &&
               s_glMakeTextureHandleNonResidentARB && hasSetter);

        if (ShouldLogBindless()) {
            if (s_Supported) {
                std::cout << "Bindless textures: SUPPORTED" << std::endl;
            } else {
                if (!extOk) {
                    std::cout << "Bindless textures: not available (missing GL_ARB_bindless_texture)" << std::endl;
                } else {
                    std::cout << "Bindless textures: not available (entry points missing)" << std::endl;
                }
            }
        }
        return s_Supported;
    }

    bool BindlessTextures::IsSupported() {
        if (!s_Inited) Init();
        return s_Supported;
    }

    std::uint64_t BindlessTextures::GetTextureHandle(GLuint texture) {
        if (!IsSupported()) return 0;
        return (std::uint64_t)s_glGetTextureHandleARB(texture);
    }

    std::uint64_t BindlessTextures::GetTextureSamplerHandle(GLuint texture, GLuint sampler) {
        if (!IsSupported() || !s_glGetTextureSamplerHandleARB) return 0;
        return (std::uint64_t)s_glGetTextureSamplerHandleARB(texture, sampler);
    }

    bool BindlessTextures::IsTextureHandleResident(std::uint64_t handle) {
        if (!IsSupported() || !s_glIsTextureHandleResidentARB || handle == 0) return false;
        return s_glIsTextureHandleResidentARB((GLuint64)handle) == GL_TRUE;
    }

    void BindlessTextures::MakeTextureHandleResident(std::uint64_t handle) {
        if (!IsSupported() || handle == 0) return;
        s_glMakeTextureHandleResidentARB((GLuint64)handle);
    }

    void BindlessTextures::MakeTextureHandleNonResident(std::uint64_t handle) {
        if (!IsSupported() || handle == 0) return;
        s_glMakeTextureHandleNonResidentARB((GLuint64)handle);
    }

    void BindlessTextures::SetUniformHandle(GLuint program, GLint location, std::uint64_t handle) {
        if (!IsSupported() || location < 0 || handle == 0) return;
        if (s_glProgramUniformHandleui64ARB) {
            s_glProgramUniformHandleui64ARB(program, location, (GLuint64)handle);
        } else if (s_glUniformHandleui64ARB) {
            s_glUniformHandleui64ARB(location, (GLuint64)handle);
        }
    }

} // namespace Engine::RendererUtil
