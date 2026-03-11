#pragma once 


#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class PressureJacobi {
    public:
    void init();
    void iterate(const VoxelDomain& domain,
               const SSBOBuffer& srcPressureBuf,
               SSBOBuffer& destPressureBuf,
               const SSBOBuffer& wallBuf,
               const SSBOBuffer& divergenceBuf);
    void destroy();

    private:
    ComputeShader shader_;

};
