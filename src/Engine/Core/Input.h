#pragma once
#include <glm/glm.hpp>
#include <string>

namespace Engine {
    class Input {
    public:
        static void Init();
        static bool IsKeyPressed(int key);
        static bool IsMouseButtonPressed(int button);
        static void SetCursorMode(bool locked);
        static void ResetRuntimeInputState(bool clearButtonsAndKeys);
        
        static glm::vec2 GetMousePosition();
        static glm::vec2 GetMouseDelta();
        static float GetScrollDeltaY();
        static std::string ConsumeTextInput();

        // Called by Window callbacks
        static void Update(); // Call at end of frame to reset delta
        static void KeyCallback(int key, int scancode, int action, int mods);
        static void MouseButtonCallback(int button, int action, int mods);
        static void CursorPositionCallback(double xpos, double ypos);
        static void ScrollCallback(double xoffset, double yoffset);
        static void CharCallback(unsigned int codepoint);

        // Replay support (used by Telemetry)
        static void SetReplayMode(bool enabled);
        static bool IsReplayMode();
        static void ReplayInjectKey(int key, bool down);
        static void ReplayInjectMouseButton(int button, bool down);
        static void ReplayInjectMouseDelta(float dx, float dy);
        static void ReplayInjectScroll(float dy);

    private:
        static bool s_Keys[1024];
        static bool s_MouseButtons[8];
        static glm::vec2 s_MousePosition;
        static glm::vec2 s_LastMousePosition;
        static glm::vec2 s_MouseDelta;
        static bool s_FirstMouse;
        static float s_ScrollDeltaY;

        static bool s_ReplayMode;
        static bool s_ReplayKeys[1024];
        static bool s_ReplayMouseButtons[8];
        static glm::vec2 s_ReplayMouseDelta;
        static float s_ReplayScrollDeltaY;
        static std::string s_TextInput;
    };
}
