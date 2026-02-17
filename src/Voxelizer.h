#ifndef VOXELIZER_H
#define VOXELIZER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "ComputeShader.h"
#include "Buffer.h"

class Voxelizer {
public:
    SSBOBuffer staticVoxels;   // binding 0: wall grid (1 = solid, 0 = empty)
    glm::ivec3 gridSize;
    glm::vec3  boundsMin;
    glm::vec3  boundsMax;
    float      voxelSize;
    int        totalVoxels;

    // Load mesh from OBJ, compute grid, run voxelize compute shader
    bool voxelizeMesh(const std::string& path, float voxSize = 0.15f) {
        voxelSize = voxSize;

        // --- Load triangles from OBJ ---
        std::vector<glm::vec3> positions;
        std::vector<glm::ivec3> faces;

        std::ifstream inFile(path);
        if (!inFile.is_open()) {
            std::cout << "Voxelizer: Failed to open " << path << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(inFile, line)) {
            std::stringstream ss(line);
            std::string type;
            ss >> type;
            if (type == "v") {
                glm::vec3 v;
                ss >> v.x >> v.y >> v.z;
                positions.push_back(v);
            }
            else if (type == "f") {
                int idx[3];
                for (int i = 0; i < 3; i++) {
                    std::string token;
                    ss >> token;
                    idx[i] = std::stoi(token.substr(0, token.find('/'))) - 1;
                }
                faces.push_back(glm::ivec3(idx[0], idx[1], idx[2]));
            }
        }
        inFile.close();

        if (positions.empty() || faces.empty()) {
            std::cout << "Voxelizer: No geometry found in " << path << std::endl;
            return false;
        }

        // --- Compute AABB ---
        boundsMin = glm::vec3(1e30f);
        boundsMax = glm::vec3(-1e30f);
        for (auto& p : positions) {
            boundsMin = glm::min(boundsMin, p);
            boundsMax = glm::max(boundsMax, p);
        }
        // Pad bounds by one voxel
        boundsMin -= glm::vec3(voxelSize);
        boundsMax += glm::vec3(voxelSize);

        glm::vec3 extent = boundsMax - boundsMin;
        gridSize = glm::ivec3(glm::ceil(extent / voxelSize));
        totalVoxels = gridSize.x * gridSize.y * gridSize.z;

        std::cout << "Voxelizer: " << faces.size() << " triangles, grid "
                  << gridSize.x << "x" << gridSize.y << "x" << gridSize.z
                  << " = " << totalVoxels << " voxels" << std::endl;

        // --- Build triangle SSBO (vec4 for std430 alignment) ---
        // struct Triangle { vec4 v0, v1, v2; } — 48 bytes each
        struct GPUTriangle {
            glm::vec4 v0, v1, v2;
        };
        std::vector<GPUTriangle> tris(faces.size());
        for (size_t i = 0; i < faces.size(); i++) {
            glm::vec3 a = positions[faces[i].x];
            glm::vec3 b = positions[faces[i].y];
            glm::vec3 c = positions[faces[i].z];
            tris[i].v0 = glm::vec4(a, 0.0f);
            tris[i].v1 = glm::vec4(b, 0.0f);
            tris[i].v2 = glm::vec4(c, 0.0f);
        }

        SSBOBuffer triBuffer;
        triBuffer.allocate(tris.size() * sizeof(GPUTriangle));
        triBuffer.upload(tris);

        // --- Allocate static voxel SSBO and clear ---
        staticVoxels.allocate(totalVoxels * sizeof(int));
        staticVoxels.clear();

        // --- Setup and dispatch compute shader ---
        ComputeShader voxCS;
        voxCS.setUp(getComputeSource());

        triBuffer.bindBase(0);      // triangles
        staticVoxels.bindBase(1);   // output voxels

        voxCS.use();
        voxCS.setIVec3("u_GridSize", gridSize);
        voxCS.setVec3("u_BoundsMin", boundsMin);
        voxCS.setFloat("u_VoxelSize", voxelSize);
        voxCS.setInt("u_TriCount", (int)faces.size());

        voxCS.dispatch((int)faces.size());
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Count filled voxels for debug
        std::vector<int> data = staticVoxels.download<int>(totalVoxels);
        int filled = 0;
        for (int v : data) if (v != 0) filled++;
        std::cout << "Voxelizer: " << filled << " filled voxels" << std::endl;

        // Cleanup
        triBuffer.destroy();
        glDeleteProgram(voxCS.ID);
        return true;
    }

    void destroy() {
        staticVoxels.destroy();
    }

private:
    const char* getComputeSource() {
        return R"(
#version 430 core
layout(local_size_x = 64) in;

// Triangle buffer: each triangle is 3 x vec4
struct Triangle { vec4 v0; vec4 v1; vec4 v2; };
layout(std430, binding = 0) readonly buffer TriBuf { Triangle triangles[]; };

// Output voxel grid
layout(std430, binding = 1) buffer VoxelBuf { int voxels[]; };

uniform ivec3 u_GridSize;
uniform vec3  u_BoundsMin;
uniform float u_VoxelSize;
uniform int   u_TriCount;

int flatIdx(ivec3 c) {
    return c.x + c.y * u_GridSize.x + c.z * u_GridSize.x * u_GridSize.y;
}

// Project all 3 vertices and the AABB half-extents onto an axis,
// return true if the intervals are separated (no overlap).
bool separatedOnAxis(vec3 axis, vec3 v0, vec3 v1, vec3 v2, vec3 halfExt) {
    float p0 = dot(axis, v0);
    float p1 = dot(axis, v1);
    float p2 = dot(axis, v2);
    float triMin = min(min(p0, p1), p2);
    float triMax = max(max(p0, p1), p2);

    // AABB projection radius onto axis
    float r = halfExt.x * abs(axis.x) + halfExt.y * abs(axis.y) + halfExt.z * abs(axis.z);

    return (triMin > r || triMax < -r);
}

// 13-axis SAT test: triangle vs AABB centered at origin with half-extent h
bool triIntersectsAABB(vec3 v0, vec3 v1, vec3 v2, vec3 h) {
    // Triangle edges
    vec3 e0 = v1 - v0;
    vec3 e1 = v2 - v1;
    vec3 e2 = v0 - v2;

    // 9 cross-product axes (edge x cardinal)
    if (separatedOnAxis(vec3(0, -e0.z, e0.y), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(0, -e1.z, e1.y), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(0, -e2.z, e2.y), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(e0.z, 0, -e0.x), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(e1.z, 0, -e1.x), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(e2.z, 0, -e2.x), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(-e0.y, e0.x, 0), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(-e1.y, e1.x, 0), v0, v1, v2, h)) return false;
    if (separatedOnAxis(vec3(-e2.y, e2.x, 0), v0, v1, v2, h)) return false;

    // 3 AABB face normals (cardinal axes)
    float triMinX = min(min(v0.x, v1.x), v2.x);
    float triMaxX = max(max(v0.x, v1.x), v2.x);
    if (triMinX > h.x || triMaxX < -h.x) return false;

    float triMinY = min(min(v0.y, v1.y), v2.y);
    float triMaxY = max(max(v0.y, v1.y), v2.y);
    if (triMinY > h.y || triMaxY < -h.y) return false;

    float triMinZ = min(min(v0.z, v1.z), v2.z);
    float triMaxZ = max(max(v0.z, v1.z), v2.z);
    if (triMinZ > h.z || triMaxZ < -h.z) return false;

    // 1 triangle face normal
    vec3 triNormal = cross(e0, e1);
    if (separatedOnAxis(triNormal, v0, v1, v2, h)) return false;

    return true;
}

void main() {
    uint triIdx = gl_GlobalInvocationID.x;
    if (triIdx >= u_TriCount) return;

    vec3 v0 = triangles[triIdx].v0.xyz;
    vec3 v1 = triangles[triIdx].v1.xyz;
    vec3 v2 = triangles[triIdx].v2.xyz;

    // Compute triangle AABB in grid coordinates
    vec3 triMin = min(min(v0, v1), v2);
    vec3 triMax = max(max(v0, v1), v2);

    ivec3 gMin = ivec3(floor((triMin - u_BoundsMin) / u_VoxelSize));
    ivec3 gMax = ivec3(floor((triMax - u_BoundsMin) / u_VoxelSize));

    gMin = max(gMin, ivec3(0));
    gMax = min(gMax, u_GridSize - 1);

    vec3 halfExt = vec3(u_VoxelSize * 0.5);

    // Test each voxel in the triangle's AABB
    for (int z = gMin.z; z <= gMax.z; z++)
    for (int y = gMin.y; y <= gMax.y; y++)
    for (int x = gMin.x; x <= gMax.x; x++) {
        // Voxel center in world space
        vec3 center = u_BoundsMin + (vec3(x, y, z) + 0.5) * u_VoxelSize;

        // Translate triangle to voxel-centered coordinates
        vec3 tv0 = v0 - center;
        vec3 tv1 = v1 - center;
        vec3 tv2 = v2 - center;

        if (triIntersectsAABB(tv0, tv1, tv2, halfExt)) {
            int idx = flatIdx(ivec3(x, y, z));
            atomicOr(voxels[idx], 1);
        }
    }
}
)";
    }
};

#endif // VOXELIZER_H
