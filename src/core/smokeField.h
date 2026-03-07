#pragma once 

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "core/Buffer.h"
#include "Voxel/VoxelDomain.h"

class SmokeField {
public:
    VoxelDomain domain;

    // smoke density scalars
    SSBOBuffer density;

};