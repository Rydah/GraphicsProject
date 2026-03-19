#pragma once 

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include "core/Buffer.h"
#include "Voxel/VoxelDomain.h"

class SmokeField {
    public:
    VoxelDomain domain;

    // The idea is that we do not want to copy large buffers over and over
    // so instead we will incur extra memory overhead by storing a seperate copy of the buffers
    // we then just swap between the old buffer and the new one by reference

    // voxel smoke density scalars
    SSBOBuffer density1;
    SSBOBuffer density2;

    // voxel smoke velocity buffers
    SSBOBuffer velocity1;
    SSBOBuffer velocity2;

    // voxel pressure buffers
    SSBOBuffer pressure1;
    SSBOBuffer pressure2;

    // Buffer for calculated divergence values
    SSBOBuffer divergence;

    bool pressure1Curr = true; // flag to indicate which pressureBuff is currently being written to. 
    // If flag == true: Then pressure1 is the readonly buffer (SRC) (the old pressure values) and pressure2 is the new buffer to write to (DEST)
    // If flag == false: Then pressure2 is the RO buffer, pressure1 is the new W buffer.
    // This is true for the other flags defined
    bool velocity1Curr = true;
    bool density1Curr = true;

    SSBOBuffer& getSrcPressure();
    SSBOBuffer& getDestPressure();
    void swapPressure();

    SSBOBuffer& getSrcVelocity();
    SSBOBuffer& getDestVelocity();
    void swapVelocity();

    SSBOBuffer& getSrcDensity();
    SSBOBuffer& getDestDensity();
    void swapDensity();


    // explicit consturctor and destructor
    void init(const VoxelDomain& domain);
    void clear();
    void destroy();
};

