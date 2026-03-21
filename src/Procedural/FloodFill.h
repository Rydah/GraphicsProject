#ifndef FLOOD_FILL_H
#define FLOOD_FILL_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include <cmath>

#include "core/ComputeShader.h"
#include "core/Buffer.h"

#include "glVersion.h"

using namespace std;

class VoxelFloodFill {
public:
    SSBOBuffer pingBuf;
    SSBOBuffer pongBuf;
    bool pingIsSrc = true;

    int seedFlatIdx = -1;
    glm::ivec3 seedCoord    = glm::ivec3(0);
    glm::vec3  seedWorldPos = glm::vec3(0);

    int maxSeedValue = 6;        // ellipsoid Y semi-axis in voxels
    float elapsedTime = 0.0f;
    float fillDuration = 1.0f;
    bool active = false;

    // ---- Ellipsoid shape control ----
    float radiusXZ = 1.2f;   // horizontal scale relative to Y
    float radiusY  = 1.0f;   // vertical scale (base)

    // Extra path-length budget beyond the ellipsoid surface, lets smoke
    // squeeze around wall gaps near the ellipsoid edge.
    float wallDetourFactor = 1.0f;

    // Max value that can be stored in the buffer (used by raymarcher to normalise).
    // Must match the floodBudget formula: maxSeedValue * maxSemiAxis * sqrt2 * wallDetourFactor
    int effectiveMaxDensity() const {
        return (int)(maxSeedValue * glm::max(radiusXZ, radiusY) * 1.4142f * wallDetourFactor) + 1;
    }

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

        seedCoord    = coord;
        seedWorldPos = worldPos;
        seedFlatIdx  = coord.x +
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

        // sqrt(2) is the geometric minimum to reach diagonal corners of the ellipsoid
        // without leaving a diamond artifact. wallDetourFactor is purely extra budget
        // for navigating around wall gaps -- tune it independently of shape.
        float maxSemiAxis = glm::max(radiusXZ, radiusY);
        int floodBudget = glm::max(1, (int)(currentSeedVal * maxSemiAxis * 1.4142f * wallDetourFactor));

        for (int i = 0; i < steps; i++) {

            SSBOBuffer& src = pingIsSrc ? pingBuf : pongBuf;
            SSBOBuffer& dst = pingIsSrc ? pongBuf : pingBuf;

            // ---- Re-seed with the large flood budget ----
            src.bindBase(1);
            seedCS.use();
            seedCS.setInt("u_SeedIdx", seedFlatIdx);
            seedCS.setInt("u_SeedVal", floodBudget);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // ---- Propagate ----
            wallBuf.bindBase(0);
            src.bindBase(1);
            dst.bindBase(2);

            fillCS.use();
            fillCS.setIVec3("u_GridSize",   gridSize);
            fillCS.setIVec3("u_SeedCoord",  seedCoord);
            fillCS.setInt  ("u_MaxSeedVal", currentSeedVal);
            fillCS.setFloat("u_RadiusXZ",   radiusXZ);
            fillCS.setFloat("u_RadiusY",    radiusY);

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
        // big explosive start then slowly expand
        return 1.0f - powf(1.0f - x, 3.0f);
    }

    const char* getSeedSource() {
    static string src = string(GLSL_VERSION_CORE) + R"(
layout(local_size_x = 1) in;
layout(std430, binding = 1) buffer Buf { int data[]; };
uniform int u_SeedIdx;
uniform int u_SeedVal;
void main() {
    data[u_SeedIdx] = max(data[u_SeedIdx], u_SeedVal);
}
)";
return src.c_str();
    }

    const char* getFillSource() {
    static string src = string(GLSL_VERSION_CORE) + R"(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(std430, binding = 0) readonly buffer WallBuf { int walls[]; };
layout(std430, binding = 1) readonly buffer SrcBuf  { int src[]; };
layout(std430, binding = 2) writeonly buffer DstBuf { int dst[]; };

uniform ivec3 u_GridSize;
uniform ivec3 u_SeedCoord;
uniform int   u_MaxSeedVal;   // current unscaled radius (grows 1..maxSeedValue)
uniform float u_RadiusXZ;
uniform float u_RadiusY;

int flatIdx(ivec3 c) {
    return c.x + c.y * u_GridSize.x + c.z * u_GridSize.x * u_GridSize.y;
}

void main() {

    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, u_GridSize))) return;

    int idx = flatIdx(coord);

    if (walls[idx] != 0) { dst[idx] = 0; return; }

    // ---- Flood-fill connectivity (uniform step cost = 1, large budget) ----
    // The budget always exceeds the ellipsoid semi-axis so in open space the
    // wavefront always reaches the boundary.  maxVal > 0 means this voxel is
    // reachable from the seed without passing through a wall.
    int maxVal = src[idx];
    ivec3 nc; int nIdx;

    nc = coord + ivec3(-1,0,0);
    if (nc.x >= 0)            { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }

    nc = coord + ivec3(1,0,0);
    if (nc.x < u_GridSize.x)  { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }

    nc = coord + ivec3(0,-1,0);
    if (nc.y >= 0)            { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }

    nc = coord + ivec3(0,1,0);
    if (nc.y < u_GridSize.y)  { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }

    nc = coord + ivec3(0,0,-1);
    if (nc.z >= 0)            { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }

    nc = coord + ivec3(0,0,1);
    if (nc.z < u_GridSize.z)  { nIdx = flatIdx(nc); if (walls[nIdx] == 0) maxVal = max(maxVal, src[nIdx] - 1); }

    // Not reachable from seed
    if (maxVal <= 0) { dst[idx] = 0; return; }

    // ---- Ellipsoid hard boundary ----
    // Voxels outside the current growing ellipsoid are zeroed.
    // The smooth density gradient is computed in the raymarcher using the
    // seed world position and ellipsoid parameters, avoiding the L1 diamond
    // artifact that would result from using the flood-fill budget directly.
    vec3 diff = vec3(coord - u_SeedCoord);
    float ex = diff.x / (float(u_MaxSeedVal) * u_RadiusXZ);
    float ey = diff.y / (float(u_MaxSeedVal) * u_RadiusY);
    float ez = diff.z / (float(u_MaxSeedVal) * u_RadiusXZ);
    if (ex*ex + ey*ey + ez*ez > 1.0) { dst[idx] = 0; return; }

    // Store the flood-fill budget so it propagates correctly in future steps.
    // The raymarch shader converts this to a smooth ellipsoid density.
    dst[idx] = max(0, maxVal);
}
)";
return src.c_str();
    }
};

#endif // FLOOD_FILL_H
