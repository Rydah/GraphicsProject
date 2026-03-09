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

private:
    ComputeShader shader_;
};