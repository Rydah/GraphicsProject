#ifndef VOXELIZER_H
#define VOXELIZER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "VoxelDomain.h"
#include "core/ComputeShader.h"
#include "core/Buffer.h"
#include "glVersion.h"

class Voxelizer {
public:
    SSBOBuffer staticVoxels;   // binding 0: wall grid (1 = solid, 0 = empty)
    // glm::ivec3 gridSize;
    // glm::vec3  boundsMin;
    // glm::vec3  boundsMax;
    // float      voxelSize;
    // int        totalVoxels;
    VoxelDomain domain;

    // Load mesh from OBJ, compute grid, run voxelize compute shader
    bool voxelizeMesh(const std::string& path, float voxSize = 0.15f) {
        domain.voxelSize = voxSize;

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
        domain.boundsMin = glm::vec3(1e30f);
        domain.boundsMax = glm::vec3(-1e30f);
        for (auto& p : positions) {
            domain.boundsMin = glm::min(domain.boundsMin, p);
            domain.boundsMax = glm::max(domain.boundsMax, p);
        }
        // Pad bounds by one voxel
        domain.boundsMin -= glm::vec3(domain.voxelSize);
        domain.boundsMax += glm::vec3(domain.voxelSize);

        glm::vec3 extent = domain.boundsMax - domain.boundsMin;
        domain.gridSize = glm::ivec3(glm::ceil(extent / domain.voxelSize));
        domain.totalVoxels = domain.gridSize.x * domain.gridSize.y * domain.gridSize.z;

        std::cout << "Voxelizer: " << faces.size() << " triangles, grid "
                  << domain.gridSize.x << "x" << domain.gridSize.y << "x" << domain.gridSize.z
                  << " = " << domain.totalVoxels << " voxels" << std::endl;

        // --- Build triangle SSBO (vec4 for std430 alignment) ---
        // struct Triangle { vec4 v0, v1, v2; } - 48 bytes each
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
        staticVoxels.allocate(domain.totalVoxels * sizeof(int));
        staticVoxels.clear();

        // --- Setup and dispatch compute shader ---
        ComputeShader voxCS;
        voxCS.setUp(getComputeSource());

        triBuffer.bindBase(0);      // triangles
        staticVoxels.bindBase(1);   // output voxels

        voxCS.use();
        voxCS.setIVec3("u_GridSize", domain.gridSize);
        voxCS.setVec3("u_BoundsMin", domain.boundsMin);
        voxCS.setFloat("u_VoxelSize", domain.voxelSize);
        voxCS.setInt("u_TriCount", (int)faces.size());

        voxCS.dispatch((int)faces.size());
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Count filled voxels for debug
        std::vector<int> data = staticVoxels.download<int>(domain.totalVoxels);
        int filled = 0;
        for (int v : data) if (v != 0) filled++;
        std::cout << "Voxelizer: " << filled << " filled voxels" << std::endl;

        // Cleanup
        triBuffer.destroy();
        glDeleteProgram(voxCS.ID);
        return true;
    }

    // Generate a procedural test scene: a wide arena with interior obstacles.
    // Outer shell uses voxel type 2 (solid but rendered transparent).
    // Interior walls use voxel type 1 (solid, opaque).
    void generateTestScene(float voxSize = 0.15f, int gridX = 96, int gridY = 64, int gridZ = 96) {
        domain.voxelSize = voxSize;
        domain.gridSize  = glm::ivec3(gridX, gridY, gridZ);
        domain.totalVoxels = gridX * gridY * gridZ;

        // Bounds centered at origin
        domain.boundsMin = glm::vec3(-(gridX * voxSize)*0.5f, -(gridY * voxSize)*0.5f, -(gridZ * voxSize)*0.5f);
        domain.boundsMax = glm::vec3( (gridX * voxSize)*0.5f,  (gridY * voxSize)*0.5f,  (gridZ * voxSize)*0.5f);

        std::vector<int> walls(domain.totalVoxels, 0);

        int NX = gridX, NY = gridY, NZ = gridZ;

        for (int z = 0; z < NZ; z++)
        for (int y = 0; y < NY; y++)
        for (int x = 0; x < NX; x++) {
            // --- Outer shell ---
            // Floor (y==0): type 1 = solid opaque
            if (y == 0) {
                walls[domain.flatten(x, y, z)] = 1;
                continue;
            }
            // Side walls (type 2 = solid barrier, rendered invisible)
            bool isShell = (x == 0 || x == NX-1 || z == 0 || z == NZ-1);
            if (isShell) {
                walls[domain.flatten(x, y, z)] = 2;
                continue;
            }

            // --- Interior obstacles (type 1 = opaque) ---

            // Long wall along X-axis at z=NZ/3, doorway gap in middle
            bool wall1 = (z == NZ/3) && (x > NX/6 && x < 5*NX/6)
                      && !(y < NY/2 && x > 2*NX/5 && x < 3*NX/5);

            // Long wall along X-axis at z=2*NZ/3, doorway gap on left side
            bool wall2 = (z == 2*NZ/3) && (x > NX/6 && x < 5*NX/6)
                      && !(y < NY/2 && x > NX/5 && x < 2*NX/5);

            // Wall along Z-axis at x=NX/4, partial height, doorway at bottom
            bool wall3 = (x == NX/4) && (z > NZ/4 && z < 3*NZ/4)
                      && !(y < NY/3 && z > 5*NZ/12 && z < 7*NZ/12);

            // Wall along Z-axis at x=3*NX/4, partial height, doorway at bottom
            bool wall4 = (x == 3*NX/4) && (z > NZ/4 && z < 3*NZ/4)
                      && !(y < NY/3 && z > 5*NZ/12 && z < 7*NZ/12);

            // Low horizontal platform / cover in one quadrant
            bool platform = (y == NY/5) && (x > NX/3 && x < NX/2) && (z > NZ/3 && z < NZ/2);

            // Small bunker box (hollow not needed - just a solid pillar)
            bool pillar1 = (x >= 2*NX/5 && x <= 2*NX/5+2) && (z >= 2*NZ/5 && z <= 2*NZ/5+2) && (y < NY/3);
            bool pillar2 = (x >= 3*NX/5 && x <= 3*NX/5+2) && (z >= 3*NZ/5 && z <= 3*NZ/5+2) && (y < NY/3);

            if (wall1 || wall2 || wall3 || wall4 || platform || pillar1 || pillar2) {
                walls[domain.flatten(x, y, z)] = 1;
            }
        }

        // Upload to GPU
        staticVoxels.allocate(domain.totalVoxels * sizeof(int));
        staticVoxels.upload(walls);

        int filled = 0;
        for (int v : walls) if (v != 0) filled++;

        std::cout << "Test scene: grid "
                << domain.gridSize.x << "x"
                << domain.gridSize.y << "x"
                << domain.gridSize.z << " = "
                << domain.totalVoxels << " voxels, "
                << filled << " walls" << std::endl;
    }

    void destroy() {
        staticVoxels.destroy();
    }

private:
    const char* getComputeSource() {
        return GLSL_VERSION_CORE 
        R"(
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
