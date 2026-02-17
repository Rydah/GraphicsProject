#ifndef FLOOD_FILL_H
#define FLOOD_FILL_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>

#include "ComputeShader.h"
#include "Buffer.h"

class VoxelFloodFill {
public:
    SSBOBuffer pingBuf;
    SSBOBuffer pongBuf;
    bool pingIsSrc = true;

    int seedFlatIdx = -1;
    int maxSeedValue = 80;      // max flood distance in voxels
    float elapsedTime = 0.0f;
    float fillDuration = 4.0f;  // seconds to reach full seed value
    bool active = false;

    void init(int totalVoxels) {
        pingBuf.allocate(totalVoxels * sizeof(int));
        pongBuf.allocate(totalVoxels * sizeof(int));
        pingBuf.clear();
        pongBuf.clear();

        seedCS.setUp(getSeedSource());
        fillCS.setUp(getFillSource());
    }

    void seed(glm::vec3 worldPos, glm::ivec3 gridSize, glm::vec3 boundsMin, float voxelSize) {
        glm::ivec3 coord = glm::ivec3(glm::floor((worldPos - boundsMin) / voxelSize));
        coord = glm::clamp(coord, glm::ivec3(0), gridSize - 1);
        seedFlatIdx = coord.x + coord.y * gridSize.x + coord.z * gridSize.x * gridSize.y;

        pingBuf.clear();
        pongBuf.clear();
        pingIsSrc = true;
        elapsedTime = 0.0f;
        active = true;

        std::cout << "Flood fill seeded at grid (" << coord.x << ", " << coord.y << ", " << coord.z << ")" << std::endl;
    }

    void propagate(int steps, glm::ivec3 gridSize, glm::vec3 boundsMin, float voxelSize,
                   const SSBOBuffer& wallBuf, float dt) {
        if (!active) return;

        elapsedTime += dt;
        float t = glm::clamp(elapsedTime / fillDuration, 0.0f, 1.0f);
        int currentSeedVal = (int)(easeIn(t) * maxSeedValue);
        if (currentSeedVal < 1) currentSeedVal = 1;

        for (int i = 0; i < steps; i++) {
            SSBOBuffer& src = pingIsSrc ? pingBuf : pongBuf;
            SSBOBuffer& dst = pingIsSrc ? pongBuf : pingBuf;

            // Re-seed: stamp the seed voxel to current growing value each step
            src.bindBase(1);
            seedCS.use();
            seedCS.setInt("u_SeedIdx", seedFlatIdx);
            seedCS.setInt("u_SeedVal", currentSeedVal);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // Propagate
            wallBuf.bindBase(0);
            src.bindBase(1);
            dst.bindBase(2);

            fillCS.use();
            fillCS.setIVec3("u_GridSize", gridSize);

            fillCS.dispatch(gridSize.x, gridSize.y, gridSize.z);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            pingIsSrc = !pingIsSrc;
        }
    }

    SSBOBuffer& currentBuffer() {
        return pingIsSrc ? pingBuf : pongBuf;
    }

    void clear() {
        pingBuf.clear();
        pongBuf.clear();
        active = false;
        elapsedTime = 0.0f;
    }

    void destroy() {
        pingBuf.destroy();
        pongBuf.destroy();
        glDeleteProgram(seedCS.ID);
        glDeleteProgram(fillCS.ID);
    }

private:
    ComputeShader seedCS;
    ComputeShader fillCS;

    float easeIn(float x) {
        if (x < 0.5f)
            return 2.0f * x * x;
        else
            return 1.0f - 1.0f / (5.0f * (2.0f * x - 0.8f) + 1.0f);
    }

    const char* getSeedSource() {
        return R"(
#version 430 core
layout(local_size_x = 1) in;
layout(std430, binding = 1) buffer Buf { int data[]; };
uniform int u_SeedIdx;
uniform int u_SeedVal;
void main() {
    data[u_SeedIdx] = max(data[u_SeedIdx], u_SeedVal);
}
)";
    }

    const char* getFillSource() {
        return R"(
#version 430 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(std430, binding = 0) readonly buffer WallBuf { int walls[]; };
layout(std430, binding = 1) readonly buffer SrcBuf  { int src[]; };
layout(std430, binding = 2) writeonly buffer DstBuf  { int dst[]; };

uniform ivec3 u_GridSize;

int flatIdx(ivec3 c) {
    return c.x + c.y * u_GridSize.x + c.z * u_GridSize.x * u_GridSize.y;
}

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, u_GridSize))) return;

    int idx = flatIdx(coord);

    // Walls block everything
    if (walls[idx] != 0) {
        dst[idx] = 0;
        return;
    }

    // This makes the fill wider than tall (ellipsoid)
    int maxVal = src[idx];

    ivec3 nc;
    int nIdx;

    nc = coord + ivec3(-1,0,0);
    if (nc.x >= 0) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }
    nc = coord + ivec3(1,0,0);
    if (nc.x < u_GridSize.x) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }
    nc = coord + ivec3(0,-1,0);
    if (nc.y >= 0) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 2); }
    nc = coord + ivec3(0,1,0);
    if (nc.y < u_GridSize.y) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 2); }
    nc = coord + ivec3(0,0,-1);
    if (nc.z >= 0) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }
    nc = coord + ivec3(0,0,1);
    if (nc.z < u_GridSize.z) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }

    dst[idx] = max(maxVal, 0);
}
)";
    }
};

#endif // FLOOD_FILL_H
