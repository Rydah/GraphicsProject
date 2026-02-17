#ifndef FLOOD_FILL_H
#define FLOOD_FILL_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>

#include "ComputeShader.h"
#include "Buffer.h"

class VoxelFloodFill {
public:
    SSBOBuffer pingBuf;   // binding 1: read buffer
    SSBOBuffer pongBuf;   // binding 2: write buffer
    bool pingIsSrc = true;

    glm::vec3 seedWorldPos = glm::vec3(0.0f);
    float maxRadius = 5.0f;
    float elapsedTime = 0.0f;
    float fillDuration = 4.0f;  // seconds to reach full radius
    bool active = false;

    void init(int totalVoxels) {
        pingBuf.allocate(totalVoxels * sizeof(int));
        pongBuf.allocate(totalVoxels * sizeof(int));
        pingBuf.clear();
        pongBuf.clear();

        seedCS.setUp(getSeedSource());
        fillCS.setUp(getFillSource());
    }

    // Seed at a world position - starts the fill
    void seed(glm::vec3 worldPos, glm::ivec3 gridSize, glm::vec3 boundsMin, float voxelSize) {
        // Convert world pos to grid coord
        glm::ivec3 coord = glm::ivec3(glm::floor((worldPos - boundsMin) / voxelSize));
        coord = glm::clamp(coord, glm::ivec3(0), gridSize - 1);
        int flatIdx = coord.x + coord.y * gridSize.x + coord.z * gridSize.x * gridSize.y;

        // Clear both buffers
        pingBuf.clear();
        pongBuf.clear();
        pingIsSrc = true;

        // Dispatch seed kernel - writes 255 at the seed voxel
        SSBOBuffer& dst = pingIsSrc ? pingBuf : pongBuf;
        dst.bindBase(1);

        seedCS.use();
        seedCS.setInt("u_SeedIdx", flatIdx);
        glDispatchCompute(1, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        seedWorldPos = worldPos;
        elapsedTime = 0.0f;
        active = true;

        std::cout << "Flood fill seeded at grid (" << coord.x << ", " << coord.y << ", " << coord.z << ")" << std::endl;
    }

    // Run N propagation steps
    void propagate(int steps, glm::ivec3 gridSize, glm::vec3 boundsMin, float voxelSize,
                   const SSBOBuffer& wallBuf, float dt) {
        if (!active) return;

        elapsedTime += dt;
        float t = glm::clamp(elapsedTime / fillDuration, 0.0f, 1.0f);
        float currentRadius = maxRadius * easeRadius(t);

        for (int i = 0; i < steps; i++) {
            SSBOBuffer& src = pingIsSrc ? pingBuf : pongBuf;
            SSBOBuffer& dst = pingIsSrc ? pongBuf : pingBuf;

            wallBuf.bindBase(0);  // static walls
            src.bindBase(1);      // read
            dst.bindBase(2);      // write

            fillCS.use();
            fillCS.setIVec3("u_GridSize", gridSize);
            fillCS.setVec3("u_BoundsMin", boundsMin);
            fillCS.setFloat("u_VoxelSize", voxelSize);
            fillCS.setVec3("u_SeedPos", seedWorldPos);
            fillCS.setFloat("u_MaxRadius", currentRadius);

            fillCS.dispatch(gridSize.x, gridSize.y, gridSize.z);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            pingIsSrc = !pingIsSrc;
        }
    }

    // Get the current read buffer (most recent result)
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

    // Easing curve from Voxelizer.cs
    float easeRadius(float x) {
        if (x < 0.5f)
            return 2.0f * x * x;
        else
            return 1.0f - 1.0f / (5.0f * (2.0f * x - 0.8f) + 1.0f);
    }

    const char* getSeedSource() {
        return R"(
#version 430 core
layout(local_size_x = 1) in;
layout(std430, binding = 1) buffer DstBuf { int dst[]; };
uniform int u_SeedIdx;
void main() {
    dst[u_SeedIdx] = 255;
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
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;
uniform vec3  u_SeedPos;
uniform float u_MaxRadius;

int flatIdx(ivec3 c) {
    return c.x + c.y * u_GridSize.x + c.z * u_GridSize.x * u_GridSize.y;
}

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, u_GridSize))) return;

    int idx = flatIdx(coord);

    // Walls block propagation
    if (walls[idx] != 0) {
        dst[idx] = 0;
        return;
    }

    // Hemisphere constraint: only propagate at or above seed Y
    vec3 worldPos = u_BoundsMin + (vec3(coord) + 0.5) * u_VoxelSize;
    if (worldPos.y < u_SeedPos.y - u_VoxelSize) {
        dst[idx] = src[idx];
        return;
    }

    // Radius constraint (hemisphere distance from seed)
    vec3 diff = worldPos - u_SeedPos;
    float dist = length(diff);
    if (dist > u_MaxRadius) {
        dst[idx] = src[idx];  // preserve existing value but don't grow
        return;
    }

    // 6-neighbor max + decay
    int maxVal = src[idx];

    // Check each neighbor inline to avoid array constructor issues
    ivec3 nc;
    int nIdx;

    nc = coord + ivec3(-1,0,0);
    if (nc.x >= 0) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }
    nc = coord + ivec3(1,0,0);
    if (nc.x < u_GridSize.x) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }
    nc = coord + ivec3(0,-1,0);
    if (nc.y >= 0) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }
    nc = coord + ivec3(0,1,0);
    if (nc.y < u_GridSize.y) { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }
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
