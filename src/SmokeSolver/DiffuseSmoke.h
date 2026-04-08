#pragma once

#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class DiffuseSmoke {
public:
    void init();

    void iterate(const VoxelDomain& domain,
                 const SSBOBuffer& srcSmokeDensityBuf,
                 SSBOBuffer& destSmokeDensityBuf,
                 const SSBOBuffer& wallBuf,
                 float dt);

    void destroy();

    void setSmokeDiffuseRate(float smokeDiffuseRate) {
        smokeDiffuseRate_ = smokeDiffuseRate;
    }

    float getSmokeDiffuseRate() {
        return smokeDiffuseRate_;
    }

private:
    ComputeShader shader_;
    float smokeDiffuseRate_ = 0.05f;
};