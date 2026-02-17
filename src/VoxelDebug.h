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
            // back face
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
             0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
            // front face
            -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f,
            // left face
            -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
            -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
            // right face
             0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f,
             0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
            // bottom face
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,
             0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f,
            // top face
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

layout(std430, binding = 1) readonly buffer VoxelBuf { int voxels[]; };

uniform mat4 u_View;
uniform mat4 u_Proj;
uniform ivec3 u_GridSize;
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;

flat out int v_Alive;
out vec3 v_Color;

void main() {
    int id = gl_InstanceID;
    int x = id % u_GridSize.x;
    int y = (id / u_GridSize.x) % u_GridSize.y;
    int z = id / (u_GridSize.x * u_GridSize.y);

    int val = voxels[id];
    v_Alive = val != 0 ? 1 : 0;

    if (v_Alive == 0) {
        // Push degenerate — GPU will discard
        gl_Position = vec4(0.0);
        return;
    }

    vec3 center = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;
    vec3 worldPos = center + aPos * u_VoxelSize * 0.9; // slight shrink for gaps

    // Color by height
    float t = float(y) / float(u_GridSize.y);
    v_Color = mix(vec3(0.2, 0.6, 1.0), vec3(1.0, 0.3, 0.2), t);

    gl_Position = u_Proj * u_View * vec4(worldPos, 1.0);
}
)";

        const char* fs = R"(
#version 430
flat in int v_Alive;
in vec3 v_Color;
out vec4 FragColor;

void main() {
    if (v_Alive == 0) discard;
    FragColor = vec4(v_Color, 0.7);
}
)";

        debugShader.setUpShader(vs, fs);
    }

    void draw(const SSBOBuffer& voxelBuf, const glm::mat4& view, const glm::mat4& proj,
              glm::ivec3 gridSize, glm::vec3 boundsMin, float voxelSize) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        voxelBuf.bindBase(1);

        debugShader.use();
        debugShader.setMat4("u_View", view);
        debugShader.setMat4("u_Proj", proj);
        debugShader.setIVec3("u_GridSize", gridSize);
        debugShader.setVec3("u_BoundsMin", boundsMin);
        debugShader.setFloat("u_VoxelSize", voxelSize);

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
