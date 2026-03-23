#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include "glVersion.h"

// Represents a movable directional/positional light source.
// The light direction sent to shaders is normalize(position), treating it
// as a distant sun-like source whose angle is determined by its world position.
//
// Orbit animation: when enabled, the light revolves around the Y-axis at a
// configurable radius, height, and angular speed.
class LightSource {
public:
    glm::vec3 color          = glm::vec3(1.0f, 0.95f, 0.9f);
    float     intensity      = 1.0f;
    float     ambientStrength = 0.35f;

    // Spherical angles — primary representation, position is derived from these.
    float azimuth   = 30.0f;   // degrees, rotation around Y
    float elevation = 60.0f;   // degrees, angle above horizon
    float sunDist   = 15.0f;   // world-space radius (affects drag scale)

    // Orbit settings
    bool  orbitEnabled = false;
    float orbitSpeed   = 0.4f;  // radians per second

    // World position — rebuilt from azimuth/elevation by rebuildPosition().
    // Also writable directly (e.g. mouse drag); call syncAngles() afterward.
    glm::vec3 position = glm::vec3(5.0f, 10.0f, 3.0f);

    LightSource() { rebuildPosition(); }

    // Recompute position from azimuth/elevation/sunDist.
    void rebuildPosition() {
        float az = glm::radians(azimuth);
        float el = glm::radians(elevation);
        position.x = sunDist * std::cos(el) * std::sin(az);
        position.y = sunDist * std::sin(el);
        position.z = sunDist * std::cos(el) * std::cos(az);
    }

    // Recompute azimuth/elevation from current position (call after mouse drag).
    void syncAngles() {
        float r   = glm::length(position);
        if (r < 1e-4f) return;
        sunDist   = r;
        elevation = glm::degrees(std::asin(position.y / r));
        azimuth   = glm::degrees(std::atan2(position.x, position.z));
    }

    void update(float dt) {
        if (!orbitEnabled) return;
        orbitAngle_ += orbitSpeed * dt;
        azimuth = glm::degrees(orbitAngle_);
        rebuildPosition();
    }

    // Normalized direction FROM the scene TOWARD the light (sun-style).
    glm::vec3 getDirection() const {
        return glm::normalize(position);
    }

    // Color pre-multiplied by intensity.
    glm::vec3 getColor() const {
        return color * intensity;
    }

    void setOrbitAngle(float radians) { orbitAngle_ = radians; }

    // Call once after GL context is ready.
    void initMarker() {
        const char* vs = GLSL_VERSION
            "layout(location=0) in vec3 aPos;\n"
            "uniform mat4 u_VP;\n"
            "uniform float u_PointSize;\n"
            "void main() { gl_Position = u_VP * vec4(aPos, 1.0); gl_PointSize = u_PointSize; }\n";

        const char* fs = GLSL_VERSION
            "out vec4 FragColor;\n"
            "uniform vec4 u_Color;\n"
            "void main() {\n"
            "    vec2 c = gl_PointCoord * 2.0 - 1.0;\n"
            "    float d = dot(c, c);\n"
            "    if (d > 1.0) discard;\n"
            "    float alpha = 1.0 - smoothstep(0.5, 1.0, d);\n"  // soft edge
            "    FragColor = vec4(u_Color.rgb, u_Color.a * alpha);\n"
            "}\n";

        markerShader_ = compileProgram_(vs, fs);
        glGenVertexArrays(1, &markerVAO_);
        glGenBuffers(1, &markerVBO_);
        glBindVertexArray(markerVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, markerVBO_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3), glm::value_ptr(position), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        markerReady_ = true;
    }

    // Draw a bright circle at the light's world position.
    void drawMarker(const glm::mat4& view, const glm::mat4& proj) {
        if (!markerReady_) return;

        // Update position in VBO
        glBindBuffer(GL_ARRAY_BUFFER, markerVBO_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(glm::vec3), glm::value_ptr(position));

        glm::mat4 vp = proj * view;
        GLint locVP    = glGetUniformLocation(markerShader_, "u_VP");
        GLint locColor = glGetUniformLocation(markerShader_, "u_Color");
        GLint locSize  = glGetUniformLocation(markerShader_, "u_PointSize");

        glUseProgram(markerShader_);
        glUniformMatrix4fv(locVP, 1, GL_FALSE, glm::value_ptr(vp));

        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(markerVAO_);

        // Outer halo — large, semi-transparent orange
        glUniform4f(locColor, 1.0f, 0.6f, 0.0f, 0.35f);
        glUniform1f(locSize, 36.0f);
        glDrawArrays(GL_POINTS, 0, 1);

        // Inner glow — medium yellow
        glUniform4f(locColor, 1.0f, 0.95f, 0.2f, 0.75f);
        glUniform1f(locSize, 20.0f);
        glDrawArrays(GL_POINTS, 0, 1);

        // Core — small bright white
        glUniform4f(locColor, 1.0f, 1.0f, 1.0f, 1.0f);
        glUniform1f(locSize, 8.0f);
        glDrawArrays(GL_POINTS, 0, 1);

        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_PROGRAM_POINT_SIZE);
    }

    void destroyMarker() {
        if (markerVAO_) { glDeleteVertexArrays(1, &markerVAO_); markerVAO_ = 0; }
        if (markerVBO_) { glDeleteBuffers(1, &markerVBO_);      markerVBO_ = 0; }
        if (markerShader_) { glDeleteProgram(markerShader_);    markerShader_ = 0; }
        markerReady_ = false;
    }

private:
    float orbitAngle_ = 0.8f;

    // Marker rendering
    bool         markerReady_  = false;
    unsigned int markerVAO_    = 0;
    unsigned int markerVBO_    = 0;
    unsigned int markerShader_ = 0;

    static unsigned int compileProgram_(const char* vs, const char* fs) {
        auto compile = [](GLenum type, const char* src) {
            unsigned int s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            return s;
        };
        unsigned int v = compile(GL_VERTEX_SHADER, vs);
        unsigned int f = compile(GL_FRAGMENT_SHADER, fs);
        unsigned int p = glCreateProgram();
        glAttachShader(p, v); glAttachShader(p, f);
        glLinkProgram(p);
        glDeleteShader(v); glDeleteShader(f);
        return p;
    }
};
