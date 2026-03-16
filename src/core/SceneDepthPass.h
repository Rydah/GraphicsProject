#ifndef SCENE_DEPTH_PASS_H
#define SCENE_DEPTH_PASS_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "core/shader.h"
#include "core/Buffer.h"
#include "core/Framebuffer.h"
#include "core/Texture2D.h"
#include "Voxel/VoxelDomain.h"
#include "glVersion.h"

// Renders the voxel scene (wall geometry) into a depth-only FBO.
// The resulting depth texture can feed into volumetric ray-marching,
// post-processing, or the DepthDebugView visualizer.
struct SceneDepthPass {
    Framebuffer depthFBO;
    Texture2D   depthTex;

    void init(int width, int height) {
        createResources(width, height);
        buildShader();
        buildCubeGeometry();
    }

    // Call when the window is resized so the depth buffer matches.
    void resize(int w, int h) {
        if (w == currentWidth && h == currentHeight) return;
        depthTex.destroy();
        depthFBO.destroy();
        createResources(w, h);
    }

    // Render wall voxels to the depth-only FBO.
    void execute(const SSBOBuffer& wallBuf,
                 const VoxelDomain& domain,
                 const glm::mat4&   view,
                 const glm::mat4&   proj)
    {
        depthFBO.bind();
        glViewport(0, 0, currentWidth, currentHeight);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        wallBuf.bindBase(0);

        depthShader.use();
        depthShader.setMat4 ("u_View",      view);
        depthShader.setMat4 ("u_Proj",      proj);
        depthShader.setIVec3("u_GridSize",  domain.gridSize);
        depthShader.setVec3 ("u_BoundsMin", domain.boundsMin);
        depthShader.setFloat("u_VoxelSize", domain.voxelSize);

        glBindVertexArray(cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, domain.totalVoxels);
        glBindVertexArray(0);

        Framebuffer::unbind();
    }

    void destroy() {
        depthTex.destroy();
        depthFBO.destroy();
        if (cubeVAO) { glDeleteVertexArrays(1, &cubeVAO); cubeVAO = 0; }
        if (cubeVBO) { glDeleteBuffers(1, &cubeVBO); cubeVBO = 0; }
        glDeleteProgram(depthShader.ID);
    }

private:
    shader       depthShader;
    unsigned int cubeVAO = 0, cubeVBO = 0;
    int          currentWidth = 0, currentHeight = 0;

    void createResources(int w, int h) {
        currentWidth  = w;
        currentHeight = h;

        // Depth texture
        depthTex.create(w, h, GL_DEPTH_COMPONENT32F);
        glBindTexture(GL_TEXTURE_2D, depthTex.ID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Depth-only FBO
        depthFBO.create();
        depthFBO.attachDepth(depthTex.ID);
        depthFBO.setDepthOnly();
        depthFBO.isComplete();
        Framebuffer::unbind();
    }

    void buildCubeGeometry() {
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
    }

    void buildShader() {
        const char* vs = GLSL_VERSION
        R"(
layout(location = 0) in vec3 aPos;

layout(std430, binding = 0) readonly buffer WallBuf { int walls[]; };

uniform mat4  u_View;
uniform mat4  u_Proj;
uniform ivec3 u_GridSize;
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;

flat out int v_IsWall;

void main() {
    int id = gl_InstanceID;

    if (walls[id] == 0) {
        v_IsWall = 0;
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    v_IsWall = 1;

    int x = id % u_GridSize.x;
    int y = (id / u_GridSize.x) % u_GridSize.y;
    int z = id / (u_GridSize.x * u_GridSize.y);

    vec3 center   = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;
    vec3 worldPos = center + aPos * u_VoxelSize;

    gl_Position = u_Proj * u_View * vec4(worldPos, 1.0);
}
)";

        const char* fs = GLSL_VERSION
        R"(
flat in int v_IsWall;

void main() {
    if (v_IsWall == 0) discard;
    // depth is written automatically by the fixed-function pipeline
}
)";

        depthShader.setUpShader(vs, fs);
    }
};

#endif // SCENE_DEPTH_PASS_H
