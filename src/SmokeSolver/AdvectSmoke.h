#pragma once

#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class AdvectSmoke {
public:
    float smokeFallOff = 0.9995f;

    // Set each frame by SmokeSolver from ApplyForces::VacuumState
    int       vacuumActive    = 0;
    glm::vec3 vacuumWorldPos  = glm::vec3(0.0f);
    float     vacuumStrength  = 0.0f;
    float     vacuumRadius    = 1.5f;

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