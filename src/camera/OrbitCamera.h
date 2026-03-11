#ifndef ORBIT_CAMERA_H
#define ORBIT_CAMERA_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Orbit camera: spherical coordinates around a target point.
//
// Controls (register the on* methods as GLFW callbacks):
//   Left-drag       : rotate (yaw / pitch)
//   Shift+left-drag : pan target
//   Scroll          : zoom
struct OrbitCamera {
    float yaw   = 45.0f;   // horizontal angle, degrees
    float pitch = 35.0f;   // vertical angle, degrees
    float dist  = 18.0f;   // distance from target
    float fovy  = 45.0f;   // vertical field of view, degrees

    glm::vec3 target = { 0.0f, 2.0f, 0.0f };
    glm::vec3 up     = { 0.0f, 1.0f, 0.0f };

    // Derived every frame
    glm::vec3 position() const {
        float yr = glm::radians(yaw);
        float pr = glm::radians(pitch);
        float x  = dist * cos(pr) * sin(yr);
        float y  = dist * sin(pr);
        float z  = dist * cos(pr) * cos(yr);
        return target + glm::vec3(x, y, z);
    }

    glm::mat4 view() const {
        return glm::lookAt(position(), target, up);
    }

    glm::mat4 proj(float aspect, float zNear = 0.001f, float zFar = 100.0f) const {
        return glm::perspective(glm::radians(fovy), aspect, zNear, zFar);
    }

    // ----- GLFW callback handlers -----

    void onMouseButton(int button, int action) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            leftHeld = (action == GLFW_PRESS);
            if (action == GLFW_PRESS) firstMouse = true;
        }
    }

    void onMouseMove(GLFWwindow* window, float x, float y) {
        if (!leftHeld) return;

        if (firstMouse) {
            prevX = x; prevY = y;
            firstMouse = false;
            return;
        }

        float dx = x - prevX;
        float dy = y - prevY;
        prevX = x; prevY = y;

        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            // Pan
            glm::vec3 pos     = position();
            glm::vec3 forward = glm::normalize(target - pos);
            glm::vec3 right   = glm::normalize(glm::cross(forward, up));
            glm::vec3 camUp   = glm::normalize(glm::cross(right, forward));
            float speed = dist * 0.002f;
            target -= right * dx * speed;
            target += camUp * dy * speed;
        } else {
            // Orbit
            yaw   -= dx * 0.3f;
            pitch += dy * 0.3f;
            pitch  = glm::clamp(pitch, -89.0f, 89.0f);
        }
    }

    void onScroll(float delta) {
        dist -= delta;
        dist  = glm::clamp(dist, 2.0f, 50.0f);
    }

private:
    bool  leftHeld   = false;
    bool  firstMouse = true;
    float prevX = 0.0f, prevY = 0.0f;
};

#endif // ORBIT_CAMERA_H
