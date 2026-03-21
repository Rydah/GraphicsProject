#ifndef DIVERGENCE_DEBUG_VIEW_H
#define DIVERGENCE_DEBUG_VIEW_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "core/shader.h"
#include "core/Buffer.h"
#include "Voxel/VoxelDomain.h"

// Visualizes divergence as instanced voxel cubes.
//
// Color convention:
//   negative divergence -> blue
//   positive divergence -> red
//   near zero           -> hidden
//
// Intended for debugging pressure projection.
// If projection is working, this should be near-zero almost everywhere
// after the projection step, except for small numerical residue or
// boundary-adjacent artifacts.
struct DivergenceDebugView {
    bool enabled = false;

    // Hide very small divergence values.
    float minAbsDivergence = 1e-4f;

    // Clamp visualization scaling at this absolute divergence.
    float maxAbsDivergence = 1.0f;

    // Cube size as a fraction of voxel size.
    float cubeScale = 0.7f;

    // Draw every Nth voxel to reduce clutter.
    int stride = 1;

    // Slice controls:
    //  -1 = disabled
    //   0 = X slice
    //   1 = Y slice
    //   2 = Z slice
    int sliceAxis = -1;
    int sliceIndex = 0;

    void init() {
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

layout(std430, binding = 0) readonly buffer DivergenceBuf { float divergence[]; };
layout(std430, binding = 1) readonly buffer WallBuf       { int walls[]; };

uniform mat4  u_View;
uniform mat4  u_Proj;
uniform ivec3 u_GridSize;
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;

uniform float u_MinAbsDivergence;
uniform float u_MaxAbsDivergence;
uniform float u_CubeScale;
uniform int   u_Stride;
uniform int   u_SliceAxis;
uniform int   u_SliceIndex;

flat out int v_Alive;
out vec3 v_Color;
out float v_Alpha;

void main() {
    int id = gl_InstanceID;

    int gx = u_GridSize.x;
    int gy = u_GridSize.y;
    int gz = u_GridSize.z;

    int x = id % gx;
    int y = (id / gx) % gy;
    int z = id / (gx * gy);

    if (walls[id] != 0) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }

    int s = max(u_Stride, 1);
    if ((x % s) != 0 || (y % s) != 0 || (z % s) != 0) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }

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

    float d = divergence[id];
    float ad = abs(d);

    if (ad < u_MinAbsDivergence) {
        v_Alive = 0;
        gl_Position = vec4(0.0);
        return;
    }

    v_Alive = 1;

    float t = clamp(ad / max(u_MaxAbsDivergence, 1e-6), 0.0, 1.0);

    vec3 negColor = vec3(0.0, 0.2, 1.0); // blue
    vec3 posColor = vec3(1.0, 0.1, 0.1); // red
    vec3 baseColor = vec3(0.08);

    v_Color = (d < 0.0)
        ? mix(baseColor, negColor, t)
        : mix(baseColor, posColor, t);

    v_Alpha = 0.12 + 0.72 * t;

    vec3 center = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;
    vec3 worldPos = center + aPos * u_VoxelSize * u_CubeScale;

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

    void draw(const VoxelDomain& domain,
              const SSBOBuffer& divergenceBuf,
              const SSBOBuffer& wallBuf,
              const glm::mat4& view,
              const glm::mat4& proj) {
        if (!enabled) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        divergenceBuf.bindBase(0);
        wallBuf.bindBase(1);

        visShader.use();
        visShader.setMat4("u_View", view);
        visShader.setMat4("u_Proj", proj);
        visShader.setIVec3("u_GridSize", domain.gridSize);
        visShader.setVec3("u_BoundsMin", domain.boundsMin);
        visShader.setFloat("u_VoxelSize", domain.voxelSize);
        visShader.setFloat("u_MinAbsDivergence", minAbsDivergence);
        visShader.setFloat("u_MaxAbsDivergence", maxAbsDivergence);
        visShader.setFloat("u_CubeScale", cubeScale);
        visShader.setInt("u_Stride", stride);
        visShader.setInt("u_SliceAxis", sliceAxis);
        visShader.setInt("u_SliceIndex", sliceIndex);

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
        if (visShader.ID) {
            glDeleteProgram(visShader.ID);
            visShader.ID = 0;
        }
    }

private:
    unsigned int cubeVAO = 0;
    unsigned int cubeVBO = 0;
    shader visShader;
};

#endif // DIVERGENCE_DEBUG_VIEW_H