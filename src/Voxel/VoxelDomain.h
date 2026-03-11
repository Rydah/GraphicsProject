#pragma once
#include <glm/glm.hpp>

// Defines the voxel grid

struct VoxelDomain {
    glm::ivec3 gridSize{0};
    glm::vec3  boundsMin{0.0f};
    glm::vec3  boundsMax{0.0f};
    float      voxelSize = 1.0f;
    int        totalVoxels = 0;

    // explicit x, y, z
    int flatten(int x, int y, int z) const {
        return x + y * gridSize.x + z * gridSize.x * gridSize.y;
    } 

    // overload for a voxel coord ivec3
    int flatten(const glm::ivec3& voxelCoord) const {
        return flatten(voxelCoord.x, voxelCoord.y, voxelCoord.z);
    }

    // modulo arithmetic to undo the flattening
    glm::ivec3 unflatten(int index) const {
        glm::ivec3 voxelCoord;
        voxelCoord.x = index % gridSize.x;
        voxelCoord.y = (index / gridSize.x) % gridSize.y;
        voxelCoord.z = index / (gridSize.x * gridSize.y);

        return voxelCoord;
    }

    // Convert a world-space position to voxel grid coordinates
    glm::ivec3 worldToGrid(const glm::vec3& p) const {
        glm::ivec3 voxelCoord = glm::ivec3(glm::floor((p - boundsMin)/voxelSize));
        return glm::clamp(voxelCoord, glm::ivec3(0), gridSize-1);
    }

    // Convert a voxel grid coord position to world-space position
    glm::vec3 gridToWorldCenter(const glm::vec3& voxelCoord) const {
        return boundsMin + (glm::vec3(voxelCoord) + 0.5f)*voxelSize; // give back world coord center of the voxel
    }

    bool inBounds(const glm::ivec3& voxelCoord) const {
        return voxelCoord.x >= 0 && voxelCoord.y >= 0 && voxelCoord.z >= 0 &&
               voxelCoord.x  < gridSize.x && voxelCoord.y < gridSize.y && voxelCoord.z < gridSize.z;
    }

};