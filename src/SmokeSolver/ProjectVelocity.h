#pragma once 

#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class ProjectVelocity {
    public:
    void init();
    void iterate(const VoxelDomain& domain,
                 const SSBOBuffer& pressureBuf,
                 const SSBOBuffer& srcVelocityBuf,
                 SSBOBuffer& destVelocityBuf,
                 const SSBOBuffer& wallBuf);
    void destroy();

    private:
    ComputeShader shader_;

};
