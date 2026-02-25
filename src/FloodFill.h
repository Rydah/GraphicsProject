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
    glm::ivec3 seedCoord = glm::ivec3(0);

    int maxSeedValue = 64;       // maximum flood radius in voxels
    float elapsedTime = 0.0f;
    float fillDuration = 4.0f;
    bool active = false;

    // ---- Ellipsoid shape control ----
    float radiusXZ = 1.0f;   // horizontal scale
    float radiusY  = 0.6f;   // vertical scale (smaller = flatter)

    void init(int totalVoxels) {
        pingBuf.allocate(totalVoxels * sizeof(int));
        pongBuf.allocate(totalVoxels * sizeof(int));
        pingBuf.clear();
        pongBuf.clear();

        seedCS.setUp(getSeedSource());
        fillCS.setUp(getFillSource());
    }

    void seed(glm::vec3 worldPos, glm::ivec3 gridSize,
              glm::vec3 boundsMin, float voxelSize) {

        glm::ivec3 coord =
            glm::ivec3(glm::floor((worldPos - boundsMin) / voxelSize));

        coord = glm::clamp(coord, glm::ivec3(0), gridSize - 1);

        seedCoord = coord;
        seedFlatIdx = coord.x +
                      coord.y * gridSize.x +
                      coord.z * gridSize.x * gridSize.y;

        pingBuf.clear();
        pongBuf.clear();
        pingIsSrc = true;
        elapsedTime = 0.0f;
        active = true;

        std::cout << "Flood fill seeded at grid ("
                  << coord.x << ", "
                  << coord.y << ", "
                  << coord.z << ")"
                  << std::endl;
    }

    void propagate(int steps,
                   glm::ivec3 gridSize,
                   glm::vec3 boundsMin,
                   float voxelSize,
                   const SSBOBuffer& wallBuf,
                   float dt) {

        if (!active) return;

        elapsedTime += dt;
        float t = glm::clamp(elapsedTime / fillDuration, 0.0f, 1.0f);
        int currentSeedVal = (int)(easeIn(t) * maxSeedValue);
        if (currentSeedVal < 1) currentSeedVal = 1;

        for (int i = 0; i < steps; i++) {

            SSBOBuffer& src = pingIsSrc ? pingBuf : pongBuf;
            SSBOBuffer& dst = pingIsSrc ? pongBuf : pingBuf;

            // ---- Re-seed growing value ----
            src.bindBase(1);
            seedCS.use();
            seedCS.setInt("u_SeedIdx", seedFlatIdx);
            seedCS.setInt("u_SeedVal", currentSeedVal);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // ---- Propagate ----
            wallBuf.bindBase(0);
            src.bindBase(1);
            dst.bindBase(2);

            fillCS.use();
            fillCS.setIVec3("u_GridSize", gridSize);
            fillCS.setIVec3("u_SeedCoord", seedCoord);
            fillCS.setInt("u_MaxSeedVal", currentSeedVal);
            fillCS.setFloat("u_RadiusXZ", radiusXZ);
            fillCS.setFloat("u_RadiusY", radiusY);

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
        // x^0.25: explosive start (near-vertical slope at t=0),
        // decelerates aggressively, then crawls through the last ~5-10%.
        // t=0.01 -> 56%, t=0.50 -> 84%, t=0.90 -> 97%, t=0.95 -> 99%
        return powf(x, 0.25f);
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
layout(std430, binding = 2) writeonly buffer DstBuf { int dst[]; };

uniform ivec3 u_GridSize;
uniform ivec3 u_SeedCoord;
uniform int   u_MaxSeedVal;
uniform float u_RadiusXZ;
uniform float u_RadiusY;

int flatIdx(ivec3 c) {
    return c.x + c.y * u_GridSize.x + c.z * u_GridSize.x * u_GridSize.y;
}

void main() {

    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, u_GridSize))) return;

    int idx = flatIdx(coord);

    // Block walls
    if (walls[idx] != 0) {
        dst[idx] = 0;
        return;
    }

    // ---- Ellipsoid constraint ----
    vec3 diff = vec3(coord - u_SeedCoord);

    float dx = diff.x / (u_MaxSeedVal * u_RadiusXZ);
    float dy = diff.y / (u_MaxSeedVal * u_RadiusY);
    float dz = diff.z / (u_MaxSeedVal * u_RadiusXZ);

    float ellipsoidDist = dx*dx + dy*dy + dz*dz;

    // Outside ellipsoid → kill value
    if (ellipsoidDist > 1.0) {
        dst[idx] = 0;
        return;
    }

    // ---- Normal propagation ----
    int maxVal = src[idx];

    ivec3 nc;
    int nIdx;

    // 6-connected neighbors
    nc = coord + ivec3(-1,0,0);
    if (nc.x >= 0) {
        nIdx = flatIdx(nc);
        if (walls[nIdx] == 0)
            maxVal = max(maxVal, src[nIdx] - 1);
    }

    nc = coord + ivec3(1,0,0);
    if (nc.x < u_GridSize.x) {
        nIdx = flatIdx(nc);
        if (walls[nIdx] == 0)
            maxVal = max(maxVal, src[nIdx] - 1);
    }

    nc = coord + ivec3(0,-1,0);
    if (nc.y >= 0) {
        nIdx = flatIdx(nc);
        if (walls[nIdx] == 0)
            maxVal = max(maxVal, src[nIdx] - 1);
    }

    nc = coord + ivec3(0,1,0);
    if (nc.y < u_GridSize.y) {
        nIdx = flatIdx(nc);
        if (walls[nIdx] == 0)
            maxVal = max(maxVal, src[nIdx] - 1);
    }

    nc = coord + ivec3(0,0,-1);
    if (nc.z >= 0) {
        nIdx = flatIdx(nc);
        if (walls[nIdx] == 0)
            maxVal = max(maxVal, src[nIdx] - 1);
    }

    nc = coord + ivec3(0,0,1);
    if (nc.z < u_GridSize.z) {
        nIdx = flatIdx(nc);
        if (walls[nIdx] == 0)
            maxVal = max(maxVal, src[nIdx] - 1);
    }

    // Flood fill reachability check (walls block, hop-count gates wavefront)
    // but store Euclidean-based value for smooth spherical iso-surfaces
    // instead of the L1 hop-count which produces an octahedral/diamond shape.
    if (maxVal <= 0) {
        dst[idx] = 0;
    } else {
        float edist = sqrt(ellipsoidDist);   // 0 at seed, 1 at ellipsoid edge
        dst[idx] = max(int(float(u_MaxSeedVal) * (1.0 - edist)), 1);
    }
}
)";
    }
};

#endif // FLOOD_FILL_H
