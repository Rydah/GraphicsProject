#pragma once

#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class AdvectVelocity {
public:
    void init();

    void iterate(const VoxelDomain& domain,
                 const SSBOBuffer& srcVelocityBuf,
                 SSBOBuffer& destVelocityBuf,
                 const SSBOBuffer& wallBuf,
                 float dt);

    void destroy();

    float smokeCoolingRate = 0.01;
private:
    ComputeShader shader_;
};