#include "Input.h"
#include "Window.h"
#include "Engine/Core/Telemetry.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>

namespace Engine {

    bool Input::s_Keys[1024];
    bool Input::s_MouseButtons[8];
    glm::vec2 Input::s_MousePosition;
    glm::vec2 Input::s_LastMousePosition;
    glm::vec2 Input::s_MouseDelta;
    bool Input::s_FirstMouse = true;
    float Input::s_ScrollDeltaY = 0.0f;

    bool Input::s_ReplayMode = false;
    bool Input::s_ReplayKeys[1024];
    bool Input::s_ReplayMouseButtons[8];
    glm::vec2 Input::s_ReplayMouseDelta;
    float Input::s_ReplayScrollDeltaY = 0.0f;
    std::string Input::s_TextInput;

    void Input::Init() {
        for (int i = 0; i < 1024; i++) s_Keys[i] = false;
        for (int i = 0; i < 8; i++) s_MouseButtons[i] = false;

        for (int i = 0; i < 1024; i++) s_ReplayKeys[i] = false;
        for (int i = 0; i < 8; i++) s_ReplayMouseButtons[i] = false;
        
        s_MousePosition = glm::vec2(0.0f);
        s_LastMousePosition = glm::vec2(0.0f);
        s_MouseDelta = glm::vec2(0.0f);
        s_ScrollDeltaY = 0.0f;

        s_ReplayMouseDelta = glm::vec2(0.0f);
        s_ReplayScrollDeltaY = 0.0f;
        s_TextInput.clear();
    }

    bool Input::IsKeyPressed(int key) {
        if (key >= 0 && key < 1024) {
            return s_ReplayMode ? s_ReplayKeys[key] : s_Keys[key];
        }
        return false;
    }

    bool Input::IsMouseButtonPressed(int button) {
        if (button >= 0 && button < 8) {
            return s_ReplayMode ? s_ReplayMouseButtons[button] : s_MouseButtons[button];
        }
        return false;
    }

    void Input::ResetRuntimeInputState(bool clearButtonsAndKeys) {
        s_MouseDelta = glm::vec2(0.0f);
        s_ScrollDeltaY = 0.0f;
        s_ReplayMouseDelta = glm::vec2(0.0f);
        s_ReplayScrollDeltaY = 0.0f;
        s_FirstMouse = true;

        if (clearButtonsAndKeys) {
            for (int i = 0; i < 1024; ++i) s_Keys[i] = false;
            for (int i = 0; i < 8; ++i) s_MouseButtons[i] = false;
        }
    }

    void Input::SetCursorMode(bool locked) {
        GLFWwindow* window = Window::GetNativeWindow();
        if (window) {
            glfwSetInputMode(window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            // Avoid stale deltas / first-frame spikes after mode changes.
            ResetRuntimeInputState(false);
        }
    }

    glm::vec2 Input::GetMousePosition() {
        return s_MousePosition;
    }

    glm::vec2 Input::GetMouseDelta() {
        return s_ReplayMode ? s_ReplayMouseDelta : s_MouseDelta;
    }

    float Input::GetScrollDeltaY() {
        return s_ReplayMode ? s_ReplayScrollDeltaY : s_ScrollDeltaY;
    }

    std::string Input::ConsumeTextInput() {
        std::string out;
        out.swap(s_TextInput);
        return out;
    }

    void Input::Update() {
        // Reset delta at the END of the frame? 
        // Or actually, delta is calculated per callback. 
        // If we want frame-based delta, we should reset it here IF we accumulate.
        // But for camera, we usually want the exact delta from the last frame.
        // We calculate delta in CursorPositionCallback.
        // So we should zero it out here if no movement happened? 
        // Use a flag? No, simpler: 
        s_MouseDelta = glm::vec2(0.0f);
        s_ScrollDeltaY = 0.0f;
        s_ReplayMouseDelta = glm::vec2(0.0f);
        s_ReplayScrollDeltaY = 0.0f;
        // Wait, if we zero it here, and the callback happens during glfwPollEvents(), 
        // then the next frame (Update) will zero it again.
        // We should call Input::Update() at the START or END of frame to clear the previous frame's accumulation.
    }

    void Input::SetReplayMode(bool enabled) {
        s_ReplayMode = enabled;
        s_FirstMouse = true;
    }

    bool Input::IsReplayMode() {
        return s_ReplayMode;
    }

    void Input::ReplayInjectKey(int key, bool down) {
        if (key >= 0 && key < 1024) s_ReplayKeys[key] = down;
    }

    void Input::ReplayInjectMouseButton(int button, bool down) {
        if (button >= 0 && button < 8) s_ReplayMouseButtons[button] = down;
    }

    void Input::ReplayInjectMouseDelta(float dx, float dy) {
        s_ReplayMouseDelta += glm::vec2(dx, dy);
    }

    void Input::ReplayInjectScroll(float dy) {
        s_ReplayScrollDeltaY += dy;
    }

    void Input::KeyCallback(int key, int scancode, int action, int mods) {
        if (s_ReplayMode) return;
        if (key >= 0 && key < 1024) {
            if (action == GLFW_PRESS)
                s_Keys[key] = true;
            else if (action == GLFW_RELEASE)
                s_Keys[key] = false;
        }

        Telemetry::RecordKey(glfwGetTime(), key, action, mods);
    }

    void Input::MouseButtonCallback(int button, int action, int mods) {
        if (s_ReplayMode) return;
        if (button >= 0 && button < 8) {
            if (action == GLFW_PRESS)
                s_MouseButtons[button] = true;
            else if (action == GLFW_RELEASE)
                s_MouseButtons[button] = false;
        }

        Telemetry::RecordMouseButton(glfwGetTime(), button, action, mods);
    }

    void Input::CursorPositionCallback(double xpos, double ypos) {
        if (s_ReplayMode) return;
        if (s_FirstMouse) {
            s_LastMousePosition = glm::vec2(xpos, ypos);
            s_FirstMouse = false;
        }

        s_MousePosition = glm::vec2(xpos, ypos);
        const glm::vec2 d((float)(xpos - s_LastMousePosition.x), (float)(s_LastMousePosition.y - ypos));
        s_MouseDelta += d; // Y inverted
        Telemetry::RecordMouseDelta(glfwGetTime(), d.x, d.y);
        s_LastMousePosition = s_MousePosition;
    }

    void Input::ScrollCallback(double xoffset, double yoffset) {
        if (s_ReplayMode) return;
        (void)xoffset;
        s_ScrollDeltaY += (float)yoffset;
        Telemetry::RecordScroll(glfwGetTime(), (float)yoffset);
    }

    void Input::CharCallback(unsigned int codepoint) {
        if (s_ReplayMode) return;
        if (codepoint < 32u || codepoint == 127u) return;

        if (codepoint <= 0x7Fu) {
            s_TextInput.push_back((char)codepoint);
        } else if (codepoint <= 0x7FFu) {
            s_TextInput.push_back((char)(0xC0u | ((codepoint >> 6u) & 0x1Fu)));
            s_TextInput.push_back((char)(0x80u | (codepoint & 0x3Fu)));
        } else if (codepoint <= 0xFFFFu) {
            s_TextInput.push_back((char)(0xE0u | ((codepoint >> 12u) & 0x0Fu)));
            s_TextInput.push_back((char)(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            s_TextInput.push_back((char)(0x80u | (codepoint & 0x3Fu)));
        } else if (codepoint <= 0x10FFFFu) {
            s_TextInput.push_back((char)(0xF0u | ((codepoint >> 18u) & 0x07u)));
            s_TextInput.push_back((char)(0x80u | ((codepoint >> 12u) & 0x3Fu)));
            s_TextInput.push_back((char)(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            s_TextInput.push_back((char)(0x80u | (codepoint & 0x3Fu)));
        }
    }
}
