#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {
    class Camera {
    public:
        Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = -90.0f, float pitch = 0.0f);

        glm::mat4 GetViewMatrix();

        void ProcessKeyboard(int direction, float deltaTime);
        void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

        glm::vec3 Position;
        glm::vec3 Front;
        glm::vec3 Up;
        glm::vec3 Right;
        glm::vec3 WorldUp;

        float Yaw;
        float Pitch;
        float MovementSpeed;
        float MouseSensitivity;
        float Zoom;

        // Movement tuning
        float AccelMult;
        float Drag;
        float MaxSpeedMult;

        glm::vec3 Velocity{0.0f}; // Current velocity
        void UpdatePhysics(float deltaTime); // Apply velocity and friction

        void updateCameraVectors();
    };
}
