#include "Window.h"
#include "Input.h"
#include "Engine/Core/RunLog.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace Engine {

    static int GetEnvIntOr(const char* name, int fallback) {
        const char* v = std::getenv(name);
        if (!v || !*v) return fallback;
        try {
            return std::stoi(v);
        } catch (...) {
            return fallback;
        }
    }

    int Window::s_Width = 1280;
    int Window::s_Height = 720;
    std::string Window::s_Title = "HighPerformanceVoxelEngine";
    GLFWwindow* Window::s_Window = nullptr;

    void Window::Init(int width, int height, const std::string& title) {
        s_Width = width;
        s_Height = height;
        s_Title = title;

        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
            RunLog::Error("Failed to initialize GLFW");
            throw std::runtime_error("Failed to initialize GLFW");
        }

        const int forcedMajor = GetEnvIntOr("HVE_GL_MAJOR", -1);
        const int forcedMinor = GetEnvIntOr("HVE_GL_MINOR", -1);

        std::vector<std::pair<int, int>> versions;
        if (forcedMajor > 0 && forcedMinor >= 0) {
            versions.push_back({forcedMajor, forcedMinor});
        } else {
            versions = {{4, 6}, {4, 5}, {4, 3}, {4, 1}, {3, 3}};
        }

        bool created = false;
        for (const auto& v : versions) {
            const int major = v.first;
            const int minor = v.second;

            glfwDefaultWindowHints();
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

            s_Window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
            if (!s_Window) {
                RunLog::Warn("GLFW window create failed for OpenGL " + std::to_string(major) + "." + std::to_string(minor));
                continue;
            }

            glfwMakeContextCurrent(s_Window);

            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                RunLog::Warn("GLAD init failed for OpenGL " + std::to_string(major) + "." + std::to_string(minor));
                glfwDestroyWindow(s_Window);
                s_Window = nullptr;
                continue;
            }

            created = true;
            RunLog::Info("OpenGL context created: requested " + std::to_string(major) + "." + std::to_string(minor));
            break;
        }

        if (!created || !s_Window) {
            std::cerr << "Failed to create / open GLFW window" << std::endl;
            RunLog::Error("Failed to create OpenGL context (tried multiple versions)");
            glfwTerminate();
            throw std::runtime_error("Failed to create OpenGL context");
        }

        // Ensure a correct initial viewport.
        glViewport(0, 0, width, height);

        std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
        std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

        glfwFocusWindow(s_Window);

        RunLog::Info(std::string("GL_VERSION: ") + (const char*)glGetString(GL_VERSION));
        RunLog::Info(std::string("GL_RENDERER: ") + (const char*)glGetString(GL_RENDERER));

        // Callbacks
        glfwSetKeyCallback(s_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            Input::KeyCallback(key, scancode, action, mods);
        });

        glfwSetMouseButtonCallback(s_Window, [](GLFWwindow* window, int button, int action, int mods) {
            Input::MouseButtonCallback(button, action, mods);
        });

        glfwSetCursorPosCallback(s_Window, [](GLFWwindow* window, double xpos, double ypos) {
            Input::CursorPositionCallback(xpos, ypos);
        });

        glfwSetScrollCallback(s_Window, [](GLFWwindow* window, double xoffset, double yoffset) {
            Input::ScrollCallback(xoffset, yoffset);
        });

        glfwSetCharCallback(s_Window, [](GLFWwindow* window, unsigned int codepoint) {
            Input::CharCallback(codepoint);
        });

        glfwSetFramebufferSizeCallback(s_Window, [](GLFWwindow* window, int width, int height) {
            const int safeW = (width > 0) ? width : 1;
            const int safeH = (height > 0) ? height : 1;
            glViewport(0, 0, safeW, safeH);
            s_Width = safeW;
            s_Height = safeH;
        });

        glfwSetWindowFocusCallback(s_Window, [](GLFWwindow* window, int focused) {
            (void)window;
            if (focused == GLFW_TRUE) {
                Input::ResetRuntimeInputState(false);
                RunLog::Info("WINDOW focus gained -> input delta reset");
            } else {
                Input::ResetRuntimeInputState(true);
                RunLog::Info("WINDOW focus lost -> input fully reset");
            }
        });

        const char* vs = std::getenv("HVE_VSYNC");
        const bool enableVsync = (vs && *vs && (vs[0] == '1' || vs[0] == 'y' || vs[0] == 'Y' || vs[0] == 't' || vs[0] == 'T'));
        glfwSwapInterval(enableVsync ? 1 : 0);
    }

    void Window::Update() {
        // Clear previous-frame deltas before polling events so callbacks during
        // glfwPollEvents() accumulate the delta we read this frame.
        Input::Update();
        glfwPollEvents();
    }

    void Window::SwapBuffers() {
        glfwSwapBuffers(s_Window);
    }

    bool Window::ShouldClose() {
        return glfwWindowShouldClose(s_Window);
    }

    void Window::Close() {
        if (s_Window) {
            glfwSetWindowShouldClose(s_Window, GLFW_TRUE);
        }
    }

    void Window::SetTitle(const std::string& title) {
        s_Title = title;
        if (s_Window) {
            glfwSetWindowTitle(s_Window, s_Title.c_str());
        }
    }

    void Window::Shutdown() {
        if (s_Window) {
            glfwDestroyWindow(s_Window);
            s_Window = nullptr;
        }
        glfwTerminate();
    }
}
