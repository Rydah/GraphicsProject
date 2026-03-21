#pragma once

#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class AdvectSmoke {
public:
    void init();

    void iterate(const VoxelDomain& domain,
                 const SSBOBuffer& srcVelocityBuf,
                 const SSBOBuffer& srcSmokeDensityBuf,
                 SSBOBuffer& destSmokeDensityBuf,
                 const SSBOBuffer& wallBuf,
                 float dt);

    void destroy();

private:
    ComputeShader shader_;
};