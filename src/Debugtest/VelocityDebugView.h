#ifndef VELOCITY_DEBUG_VIEW_H
#define VELOCITY_DEBUG_VIEW_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "core/shader.h"
#include "core/Buffer.h"
#include "Voxel/VoxelDomain.h"

// Draws a voxelized debug view of the velocity field.
// Intended for quick visualization of where motion exists in the grid.
// Color indicates speed magnitude.
struct VelocityDebugView {
    bool  enabled   = false;
    float minSpeed  = 0.001f; // hide very small velocities
    float maxSpeed  = 2.0f;   // visualization scale

    void init() {
        // Unit cube vertices (36 verts, 12 triangles)
        float verts[] = {
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
             0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
            -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f,
            -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
            -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f,
             0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,
             0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f,
            -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f,
        };

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);

        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        const char* vs = R"(
#version 430 core
layout(location = 0) in vec3 aPos;

layout(std430, binding = 0) readonly buffer VelocityBuf { vec4 velocity[]; };
layout(std430, binding = 1) readonly buffer WallBuf     { int walls[]; };

uniform mat4 u_View;
uniform mat4 u_Proj;
uniform ivec3 u_GridSize;
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;
uniform float u_MinSpeed;
uniform float u_MaxSpeed;

flat out int v_Alive;
out vec3 v_Color;
out float v_Alpha;

void main() {
    int id = gl_InstanceID;
    int x = id % u_GridSize.x;
    int y = (id / u_GridSize.x) % u_GridSize.y;
    int z = id / (u_GridSize.x * u_GridSize.y);

    if (walls[id] != 0) {
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

    float t = clamp(speed / max(u_MaxSpeed, 1e-6), 0.0, 1.0);

    // Blue -> Cyan -> Yellow -> Red
    vec3 c0 = vec3(0.1, 0.2, 1.0);
    vec3 c1 = vec3(0.0, 1.0, 1.0);
    vec3 c2 = vec3(1.0, 1.0, 0.0);
    vec3 c3 = vec3(1.0, 0.2, 0.0);

    if (t < 0.33) {
        v_Color = mix(c0, c1, t / 0.33);
    } else if (t < 0.66) {
        v_Color = mix(c1, c2, (t - 0.33) / 0.33);
    } else {
        v_Color = mix(c2, c3, (t - 0.66) / 0.34);
    }

    v_Alpha = 0.25 + 0.55 * t;

    vec3 center = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;
    vec3 worldPos = center + aPos * u_VoxelSize * 0.7;

    gl_Position = u_Proj * u_View * vec4(worldPos, 1.0);
}
)";

        const char* fs = R"(
#version 430 core

flat in int v_Alive;
in vec3 v_Color;
in float v_Alpha;

out vec4 FragColor;

void main() {
    if (v_Alive == 0) discard;
    FragColor = vec4(v_Color, v_Alpha);
}
)";

        visShader.setUpShader(vs, fs);
    }

    // Call inside the render loop when enabled.
    void draw(const VoxelDomain& domain,
              const SSBOBuffer& velocityBuf,
              const SSBOBuffer& wallBuf,
              const glm::mat4& view,
              const glm::mat4& proj) {
        if (!enabled) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

        glBindVertexArray(cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, domain.totalVoxels);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }

    void destroy() {
        if (cubeVAO) {
            glDeleteVertexArrays(1, &cubeVAO);
            cubeVAO = 0;
        }
        if (cubeVBO) {
            glDeleteBuffers(1, &cubeVBO);
            cubeVBO = 0;
        }
        glDeleteProgram(visShader.ID);
    }

private:
    unsigned int cubeVAO = 0;
    unsigned int cubeVBO = 0;
    shader visShader;
};

#endif // VELOCITY_DEBUG_VIEW_H