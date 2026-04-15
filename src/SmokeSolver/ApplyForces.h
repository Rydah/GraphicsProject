#pragma once
#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class ApplyForces {
public:
    int buoyancyMode = 0;
    float gravityStrength  = 0.05f;
    float BaroclinicStrength = 0.15f;

    // Legacy implementation
    float buoyancyStrength = 1.0f;
    float densityLow = 0.5f;  // floats below this density
    float densityHigh = 0.9f; // float above this density

    // heat based implementation
    float tempBounyancyStrength = 1.0f;

    void init();
    void dispatch(
                const VoxelDomain& domain,
                const SSBOBuffer& velocitySrc,
                const SSBOBuffer& velocityDst,
                const SSBOBuffer& smokeBuf,
                const SSBOBuffer& wallBuf,
                float dt);

    void destroy();

private:
    ComputeShader forceCS;
};