#pragma once

#include <string>

struct GLFWwindow;

namespace Engine {

    class Window {
    public:
        static void Init(int width, int height, const std::string& title);
        static void Update();
        static void SwapBuffers();
        static bool ShouldClose();
        static void Shutdown();
        static void Close();

        static void SetTitle(const std::string& title);

        static int GetWidth() { return s_Width; }
        static int GetHeight() { return s_Height; }
        static GLFWwindow* GetNativeWindow() { return s_Window; }

    private:
        static int s_Width;
        static int s_Height;
        static std::string s_Title;
        static GLFWwindow* s_Window;
    };

}
