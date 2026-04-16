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

    struct VacuumState {
        bool      active   = false;
        float     elapsed  = 0.0f;
        float     duration = 3.0f;    // seconds before auto-dissipation
        float     strength = 20.0f;   // inward acceleration (world-units/s^2)
        float     radius   = 3.5f;    // world-space influence radius
        glm::vec3 worldPos = glm::vec3(0.0f);
    } vacuum;

    void activateVacuum(glm::vec3 pos) {
        vacuum.worldPos = pos;
        vacuum.elapsed  = 0.0f;
        vacuum.active   = true;
    }

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