#ifndef VOXEL_DEBUG_H
#define VOXEL_DEBUG_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "core/shader.h"
#include "core/Buffer.h"
#include "glVersion.h"

class VoxelDebug {
public:
    unsigned int cubeVAO = 0, cubeVBO = 0;
    shader debugShader;

    void init() {
        // Unit cube: position (3) + normal (3), 36 verts
        float verts[] = {
            // -Z face  normal  0, 0,-1
            -0.5f,-0.5f,-0.5f,  0, 0,-1,   0.5f,-0.5f,-0.5f,  0, 0,-1,   0.5f, 0.5f,-0.5f,  0, 0,-1,
             0.5f, 0.5f,-0.5f,  0, 0,-1,  -0.5f, 0.5f,-0.5f,  0, 0,-1,  -0.5f,-0.5f,-0.5f,  0, 0,-1,
            // +Z face  normal  0, 0,+1
            -0.5f,-0.5f, 0.5f,  0, 0, 1,   0.5f,-0.5f, 0.5f,  0, 0, 1,   0.5f, 0.5f, 0.5f,  0, 0, 1,
             0.5f, 0.5f, 0.5f,  0, 0, 1,  -0.5f, 0.5f, 0.5f,  0, 0, 1,  -0.5f,-0.5f, 0.5f,  0, 0, 1,
            // -X face  normal -1, 0, 0
            -0.5f, 0.5f, 0.5f, -1, 0, 0,  -0.5f, 0.5f,-0.5f, -1, 0, 0,  -0.5f,-0.5f,-0.5f, -1, 0, 0,
            -0.5f,-0.5f,-0.5f, -1, 0, 0,  -0.5f,-0.5f, 0.5f, -1, 0, 0,  -0.5f, 0.5f, 0.5f, -1, 0, 0,
            // +X face  normal +1, 0, 0
             0.5f, 0.5f, 0.5f,  1, 0, 0,   0.5f, 0.5f,-0.5f,  1, 0, 0,   0.5f,-0.5f,-0.5f,  1, 0, 0,
             0.5f,-0.5f,-0.5f,  1, 0, 0,   0.5f,-0.5f, 0.5f,  1, 0, 0,   0.5f, 0.5f, 0.5f,  1, 0, 0,
            // -Y face  normal  0,-1, 0
            -0.5f,-0.5f,-0.5f,  0,-1, 0,   0.5f,-0.5f,-0.5f,  0,-1, 0,   0.5f,-0.5f, 0.5f,  0,-1, 0,
             0.5f,-0.5f, 0.5f,  0,-1, 0,  -0.5f,-0.5f, 0.5f,  0,-1, 0,  -0.5f,-0.5f,-0.5f,  0,-1, 0,
            // +Y face  normal  0,+1, 0
            -0.5f, 0.5f,-0.5f,  0, 1, 0,   0.5f, 0.5f,-0.5f,  0, 1, 0,   0.5f, 0.5f, 0.5f,  0, 1, 0,
             0.5f, 0.5f, 0.5f,  0, 1, 0,  -0.5f, 0.5f, 0.5f,  0, 1, 0,  -0.5f, 0.5f,-0.5f,  0, 1, 0,
        };

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        const char* vs = GLSL_VERSION
        R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

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
flat out vec3 v_Normal;

void main() {
    int id = gl_InstanceID;
    int x = id % u_GridSize.x;
    int y = (id / u_GridSize.x) % u_GridSize.y;
    int z = id / (u_GridSize.x * u_GridSize.y);

    int wallVal = walls[id];
    int smokeVal = (u_Mode == 1) ? smoke[id] : 0;

    bool isWall = wallVal == 1;
    bool isSmoke = smokeVal > 0;
    // mode 0 = walls only, mode 1 = smoke only, mode 2 = walls+smoke (unused)
    if (u_Mode == 0) v_Alive = isWall  ? 1 : 0;
    else             v_Alive = isSmoke ? 1 : 0;

    if (v_Alive == 0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    // Cull faces whose neighbor in the normal direction is also solid
    // (internal shared faces are never visible and cause z-fighting seams)
    if (isWall) {
        ivec3 nb = ivec3(x, y, z) + ivec3(aNormal);
        if (all(greaterThanEqual(nb, ivec3(0))) && all(lessThan(nb, u_GridSize))) {
            int nbIdx = nb.x + nb.y * u_GridSize.x + nb.z * u_GridSize.x * u_GridSize.y;
            if (walls[nbIdx] != 0) {
                gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
                return;
            }
        }
    }

    vec3 center = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;
    vec3 worldPos = center + aPos * u_VoxelSize;

    if (isWall) {
        if (y == 0) {
            int checker = (x + z) & 1;
            v_Color = (checker == 0) ? vec3(0.2, 0.2, 0.2) : vec3(0.60, 0.58, 0.62);
        } else {
            v_Color = vec3(0.7, 0.7, 0.7);
        }
        v_Alpha = 1.0;
    } else {
        // Smoke: orange-to-white by density
        float d = float(smokeVal) / 255.0;
        v_Color = mix(vec3(1.0, 0.4, 0.1), vec3(1.0, 1.0, 1.0), d);
        v_Alpha = clamp(sqrt(d) * 0.95, 0.0, 1.0);
    }

    v_Normal = aNormal;
    gl_Position = u_Proj * u_View * vec4(worldPos, 1.0);
}
)";

        const char* fs = GLSL_VERSION
        R"(
flat in int v_Alive;
in vec3 v_Color;
in float v_Alpha;
flat in vec3 v_Normal;
out vec4 FragColor;

void main() {
    if (v_Alive == 0) discard;

    // Simple directional light in world space
    vec3 lightDir = normalize(vec3(0.6, 1.0, 0.5));
    float ambient  = 0.35;
    float diffuse  = max(dot(v_Normal, lightDir), 0.0) * 0.65;
    float light    = ambient + diffuse;

    FragColor = vec4(v_Color * light, v_Alpha);
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
        int total = gridSize.x * gridSize.y * gridSize.z;

        wallBuf.bindBase(0);
        smokeBuf.bindBase(1);

        debugShader.use();
        debugShader.setMat4("u_View", view);
        debugShader.setMat4("u_Proj", proj);
        debugShader.setIVec3("u_GridSize", gridSize);
        debugShader.setVec3("u_BoundsMin", boundsMin);
        debugShader.setFloat("u_VoxelSize", voxelSize);

        // Pass 1: opaque walls — depth writes ON so smoke tests against them
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        debugShader.setInt("u_Mode", 0);
        glBindVertexArray(cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, total);
        glBindVertexArray(0);

        // Pass 2: smoke only — no depth writes
        glDepthMask(GL_FALSE);
        debugShader.setInt("u_Mode", 1);
        glBindVertexArray(cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, total);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    void destroy() {
        if (cubeVAO) { glDeleteVertexArrays(1, &cubeVAO); cubeVAO = 0; }
        if (cubeVBO) { glDeleteBuffers(1, &cubeVBO); cubeVBO = 0; }
        glDeleteProgram(debugShader.ID);
    }
};

#endif // VOXEL_DEBUG_H
