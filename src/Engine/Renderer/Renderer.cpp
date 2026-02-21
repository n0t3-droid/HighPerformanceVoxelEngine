#include "Engine/Renderer/Renderer.h"
#include <glad/glad.h>
#include <iostream>
#include <GLFW/glfw3.h>
#include <cstdlib>

#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif

#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#endif

#ifndef GL_DEBUG_OUTPUT_SYNCHRONOUS
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#endif

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

// Minimal KHR_debug function pointer types (some GLAD builds omit them)
typedef void (APIENTRY *GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity,
                                    GLsizei length, const GLchar* message, const void* userParam);
typedef void (APIENTRY *PFNGLDEBUGMESSAGECALLBACKPROC)(GLDEBUGPROC callback, const void* userParam);
typedef void (APIENTRY *PFNGLDEBUGMESSAGECONTROLPROC)(GLenum source, GLenum type, GLenum severity,
                                                     GLsizei count, const GLuint* ids, GLboolean enabled);

namespace Engine {
    static PFNGLDEBUGMESSAGECALLBACKPROC s_glDebugMessageCallback = nullptr;
    static PFNGLDEBUGMESSAGECONTROLPROC s_glDebugMessageControl = nullptr;

    static void TryLoadDebugOutputFunctions() {
        if (s_glDebugMessageCallback && s_glDebugMessageControl) return;
        s_glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)glfwGetProcAddress("glDebugMessageCallback");
        s_glDebugMessageControl = (PFNGLDEBUGMESSAGECONTROLPROC)glfwGetProcAddress("glDebugMessageControl");
    }

    static void APIENTRY GLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                        GLsizei length, const GLchar* message, const void* userParam) {
        (void)source;
        (void)type;
        (void)length;
        (void)userParam;

        // Skip extremely noisy notifications.
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

        std::cerr << "[GL] id=" << id << " severity=0x" << std::hex << severity << std::dec
                  << " msg=" << (message ? message : "<null>") << std::endl;
    }

    void Renderer::Init() {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        // Debug output (KHR_debug / GL 4.3+)
        const char* dbg = std::getenv("HVE_GL_DEBUG");
        const bool enableDebug = !(dbg && *dbg && (dbg[0] == '0' || dbg[0] == 'n' || dbg[0] == 'N' || dbg[0] == 'f' || dbg[0] == 'F'));
        if (!enableDebug) return;

        TryLoadDebugOutputFunctions();
        if (s_glDebugMessageCallback && s_glDebugMessageControl) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            s_glDebugMessageCallback(GLDebugCallback, nullptr);
            // Disable notifications to keep logs readable
            s_glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        }
    }

    void Renderer::Shutdown() {
        // Cleanup if needed
    }

    void Renderer::Clear() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void Renderer::SetClearColor(const glm::vec4& color) {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void Renderer::BeginFrame() {
        // Setup per-frame state
    }

    void Renderer::EndFrame() {
        // Flush commands if needed
    }
}
