#ifndef VOXEL_DEBUG_H
#define VOXEL_DEBUG_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "shader.h"
#include "Buffer.h"

class VoxelDebug {
public:
    unsigned int cubeVAO = 0, cubeVBO = 0;
    shader debugShader;

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
#version 430
layout(location = 0) in vec3 aPos;

layout(std430, binding = 0) readonly buffer WallBuf  { int walls[]; };
layout(std430, binding = 1) readonly buffer SmokeBuf { int smoke[]; };

uniform mat4 u_View;
uniform mat4 u_Proj;
uniform ivec3 u_GridSize;
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;
uniform int   u_Mode;  // 0 = walls only, 1 = smoke + walls

flat out int v_Alive;
out vec3 v_Color;
out float v_Alpha;

void main() {
    int id = gl_InstanceID;
    int x = id % u_GridSize.x;
    int y = (id / u_GridSize.x) % u_GridSize.y;
    int z = id / (u_GridSize.x * u_GridSize.y);

    int wallVal = walls[id];
    int smokeVal = (u_Mode == 1) ? smoke[id] : 0;

    bool isWall = wallVal != 0;
    bool isSmoke = smokeVal > 0;
    v_Alive = (isWall || isSmoke) ? 1 : 0;

    if (v_Alive == 0) {
        gl_Position = vec4(0.0);
        return;
    }

    vec3 center = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;
    vec3 worldPos = center + aPos * u_VoxelSize * 0.9;

    if (isWall) {
        float t = float(y) / float(u_GridSize.y);
        v_Color = mix(vec3(0.2, 0.5, 0.8), vec3(0.3, 0.6, 1.0), t);
        v_Alpha = 0.3;
    } else {
        // Smoke: orange-to-white by density
        float d = float(smokeVal) / 255.0;
        v_Color = mix(vec3(1.0, 0.4, 0.1), vec3(1.0, 1.0, 1.0), d);
        v_Alpha = d * 0.8;
    }

    gl_Position = u_Proj * u_View * vec4(worldPos, 1.0);
}
)";

        const char* fs = R"(
#version 430
flat in int v_Alive;
in vec3 v_Color;
in float v_Alpha;
out vec4 FragColor;

void main() {
    if (v_Alive == 0) discard;
    FragColor = vec4(v_Color, v_Alpha);
}
)";

        debugShader.setUpShader(vs, fs);
    }

    // Draw walls only (mode 0)
    void draw(const SSBOBuffer& wallBuf, const glm::mat4& view, const glm::mat4& proj,
              glm::ivec3 gridSize, glm::vec3 boundsMin, float voxelSize) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        wallBuf.bindBase(0);

        debugShader.use();
        debugShader.setMat4("u_View", view);
        debugShader.setMat4("u_Proj", proj);
        debugShader.setIVec3("u_GridSize", gridSize);
        debugShader.setVec3("u_BoundsMin", boundsMin);
        debugShader.setFloat("u_VoxelSize", voxelSize);
        debugShader.setInt("u_Mode", 0);

        glBindVertexArray(cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, gridSize.x * gridSize.y * gridSize.z);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }

    // Draw walls + smoke density (mode 1)
    void drawWithSmoke(const SSBOBuffer& wallBuf, const SSBOBuffer& smokeBuf,
                       const glm::mat4& view, const glm::mat4& proj,
                       glm::ivec3 gridSize, glm::vec3 boundsMin, float voxelSize) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        wallBuf.bindBase(0);
        smokeBuf.bindBase(1);

        debugShader.use();
        debugShader.setMat4("u_View", view);
        debugShader.setMat4("u_Proj", proj);
        debugShader.setIVec3("u_GridSize", gridSize);
        debugShader.setVec3("u_BoundsMin", boundsMin);
        debugShader.setFloat("u_VoxelSize", voxelSize);
        debugShader.setInt("u_Mode", 1);

        glBindVertexArray(cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, gridSize.x * gridSize.y * gridSize.z);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }

    void destroy() {
        if (cubeVAO) { glDeleteVertexArrays(1, &cubeVAO); cubeVAO = 0; }
        if (cubeVBO) { glDeleteBuffers(1, &cubeVBO); cubeVBO = 0; }
        glDeleteProgram(debugShader.ID);
    }
};

#endif // VOXEL_DEBUG_H
