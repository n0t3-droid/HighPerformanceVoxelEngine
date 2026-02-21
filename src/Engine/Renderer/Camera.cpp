#include "Engine/Renderer/Camera.h"

namespace Engine {
    Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) 
        : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(10.0f), MouseSensitivity(0.1f), Zoom(45.0f),
          AccelMult(8.0f), Drag(6.0f), MaxSpeedMult(1.8f) {
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        updateCameraVectors();
    }

    glm::mat4 Camera::GetViewMatrix() {
        return glm::lookAt(Position, Position + Front, Up);
    }

    void Camera::ProcessKeyboard(int direction, float deltaTime) {
        // Use acceleration instead of direct position modification for smoother feel
        // but keep it responsive.
        float accel = MovementSpeed * AccelMult * deltaTime; 
        
        if (direction == 0) Velocity += Front * accel;
        if (direction == 1) Velocity -= Front * accel;
        if (direction == 2) Velocity -= Right * accel;
        if (direction == 3) Velocity += Right * accel;
        if (direction == 4) Velocity += WorldUp * accel;
        if (direction == 5) Velocity -= WorldUp * accel;
    }

    void Camera::UpdatePhysics(float deltaTime) {
        const float maxSpeed = MovementSpeed * MaxSpeedMult;
        const float speed = glm::length(Velocity);
        if (speed > maxSpeed && speed > 0.0f) {
            Velocity = (Velocity / speed) * maxSpeed;
        }
        Position += Velocity * deltaTime;
        // Apply Drag/Friction to stop when keys are released
        Velocity += -Velocity * Drag * deltaTime;
        
        // Clamp near zero to prevent float drift
        if (glm::length(Velocity) < 0.01f) Velocity = glm::vec3(0.0f);
    }
    
    void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw   += xoffset;
        Pitch += yoffset;

        if (constrainPitch) {
            if (Pitch > 89.0f)
                Pitch = 89.0f;
            if (Pitch < -89.0f)
                Pitch = -89.0f;
        }

        updateCameraVectors();
    }

    void Camera::updateCameraVectors() {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp)); 
        Up    = glm::normalize(glm::cross(Right, Front));
    }
}
