#ifndef VELOCITY_DEBUG_VIEW_H
#define VELOCITY_DEBUG_VIEW_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "core/shader.h"
#include "core/Buffer.h"
#include "Voxel/VoxelDomain.h"

// Debug view for visualizing voxel velocity as instanced line glyphs.
//
// Each active voxel draws a line:
//   start = voxel center
//   end   = voxel center + normalized(velocity) * scaled_length
//
// Benefits over cube-magnitude view:
// - shows actual flow direction
// - easier to see curl / transport / boundary issues
// - less visually noisy when stride > 1
//
// Requires:
// - velocity SSBO bound to binding = 0, storing vec4 velocity[]
// - wall SSBO bound to binding = 1, storing int walls[]
struct VelocityDebugView {
    enum class ColorMode {
        Direction = 0, // color encodes direction
        Speed     = 1  // color encodes speed magnitude
    };

    bool enabled = false;

    // Hide tiny velocities.
    float minSpeed = 0.001f;

    // Speed at/above this clamps the visualization length/color scale.
    float maxSpeed = 2.0f;

    // Length multiplier in voxel units.
    // 1.0 means max-length arrows are about one voxel long.
    float lengthScale = 0.5f;

    // Draw only every Nth voxel in each axis to reduce clutter.
    int stride = 2;

    // Slice controls:
    //  -1 = disabled
    //   0 = X slice
    //   1 = Y slice
    //   2 = Z slice
    int sliceAxis  = -1;
    int sliceIndex = 0;

    // Line width for glyph rendering.
    // Note: modern OpenGL drivers may clamp this to 1.0.
    float lineWidth = 1.5f;

    ColorMode colorMode = ColorMode::Direction;

    void init() {
        // Empty VAO is enough because we generate the two line endpoints
        // in the vertex shader using gl_VertexID.
        glGenVertexArrays(1, &lineVAO);

        const char* vs = R"(
#version 430 core

layout(std430, binding = 0) readonly buffer VelocityBuf { vec4 velocity[]; };
layout(std430, binding = 1) readonly buffer WallBuf     { int walls[]; };

uniform mat4  u_View;
uniform mat4  u_Proj;
uniform ivec3 u_GridSize;
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;

uniform float u_MinSpeed;
uniform float u_MaxSpeed;
uniform float u_LengthScale;
uniform int   u_Stride;
uniform int   u_SliceAxis;
uniform int   u_SliceIndex;
uniform int   u_ColorMode;

flat out int v_Alive;
out vec3 v_Color;

vec3 speedRamp(float t) {
    // Blue -> Cyan -> Yellow -> Red
    vec3 c0 = vec3(0.1, 0.2, 1.0);
    vec3 c1 = vec3(0.0, 1.0, 1.0);
    vec3 c2 = vec3(1.0, 1.0, 0.0);
    vec3 c3 = vec3(1.0, 0.2, 0.0);

    if (t < 0.33) {
        return mix(c0, c1, t / 0.33);
    } else if (t < 0.66) {
        return mix(c1, c2, (t - 0.33) / 0.33);
    } else {
        return mix(c2, c3, (t - 0.66) / 0.34);
    }
}

void main() {
    int id = gl_InstanceID;

    int gx = u_GridSize.x;
    int gy = u_GridSize.y;
    int gz = u_GridSize.z;

    int x = id % gx;
    int y = (id / gx) % gy;
    int z = id / (gx * gy);

    // Skip walls
    if (walls[id] != 0) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }

    // Stride filter
    int s = max(u_Stride, 1);
    if ((x % s) != 0 || (y % s) != 0 || (z % s) != 0) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }

    // Slice filter
    if (u_SliceAxis == 0 && x != u_SliceIndex) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }
    if (u_SliceAxis == 1 && y != u_SliceIndex) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }
    if (u_SliceAxis == 2 && z != u_SliceIndex) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }

    vec3 vel = velocity[id].xyz;
    float speed = length(vel);

    if (speed < u_MinSpeed) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }

    v_Alive = 1;

    vec3 center = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;
    vec3 dir = vel / max(speed, 1e-6);

    float t = clamp(speed / max(u_MaxSpeed, 1e-6), 0.0, 1.0);

    // Start at center, end along direction
    float lineLen = u_VoxelSize * u_LengthScale * speed;
    vec3 startPos = center;
    vec3 endPos   = center + dir * lineLen;

    vec3 worldPos = (gl_VertexID == 0) ? startPos : endPos;

    if (u_ColorMode == 0) {
        // Direction-based coloring: map [-1, 1] -> [0, 1]
        v_Color = 0.5 + 0.5 * dir;
    } else {
        // Speed-based coloring
        v_Color = speedRamp(t);
    }

    gl_Position = u_Proj * u_View * vec4(worldPos, 1.0);
}
)";

        const char* fs = R"(
#version 430 core

flat in int v_Alive;
in vec3 v_Color;

out vec4 FragColor;

void main() {
    if (v_Alive == 0) discard;
    FragColor = vec4(v_Color, 1.0);
}
)";

        visShader.setUpShader(vs, fs);
    }

    void draw(const VoxelDomain& domain,
              const SSBOBuffer& velocityBuf,
              const SSBOBuffer& wallBuf,
              const glm::mat4& view,
              const glm::mat4& proj) {
        if (!enabled) return;

        velocityBuf.bindBase(0);
        wallBuf.bindBase(1);

        visShader.use();
        visShader.setMat4("u_View", view);
        visShader.setMat4("u_Proj", proj);
        visShader.setIVec3("u_GridSize", domain.gridSize);
        visShader.setVec3("u_BoundsMin", domain.boundsMin);
        visShader.setFloat("u_VoxelSize", domain.voxelSize);
        visShader.setFloat("u_MinSpeed", minSpeed);
        visShader.setFloat("u_MaxSpeed", maxSpeed);
        visShader.setFloat("u_LengthScale", lengthScale);
        visShader.setInt("u_Stride", stride);
        visShader.setInt("u_SliceAxis", sliceAxis);
        visShader.setInt("u_SliceIndex", sliceIndex);
        visShader.setInt("u_ColorMode", static_cast<int>(colorMode));

        GLfloat oldLineWidth = 1.0f;
        glGetFloatv(GL_LINE_WIDTH, &oldLineWidth);
        glLineWidth(lineWidth);

        glBindVertexArray(lineVAO);
        glDrawArraysInstanced(GL_LINES, 0, 2, domain.totalVoxels);
        glBindVertexArray(0);

        glLineWidth(oldLineWidth);
    }

    void destroy() {
        if (lineVAO) {
            glDeleteVertexArrays(1, &lineVAO);
            lineVAO = 0;
        }

        if (visShader.ID) {
            glDeleteProgram(visShader.ID);
            visShader.ID = 0;
        }
    }

private:
    unsigned int lineVAO = 0;
    shader visShader;
};

#endif // VELOCITY_DEBUG_VIEW_H