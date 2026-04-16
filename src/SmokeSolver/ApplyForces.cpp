#include "ApplyForces.h"

void ApplyForces::init() {
    forceCS.setUpFromFile("shaders/smoke/ApplyForces.comp");
}

void ApplyForces::dispatch(
                const VoxelDomain& domain,
                const SSBOBuffer& velocitySrc,
                const SSBOBuffer& velocityDst,
                const SSBOBuffer& smokeBuf,
                const SSBOBuffer& wallBuf,
                float dt)
{
    forceCS.use();

    velocitySrc.bindBase(0);
    velocityDst.bindBase(1);
    smokeBuf.bindBase(2);
    wallBuf.bindBase(3);

    forceCS.setIVec3("u_GridSize", domain.gridSize);
    forceCS.setFloat("u_CellSize", domain.voxelSize);
    forceCS.setFloat("u_Dt", dt);
    forceCS.setFloat("u_GravityStrength", gravityStrength);
    forceCS.setFloat("u_BuoyancyStrength", buoyancyStrength);
    forceCS.setInt("u_BuoyancyMode", buoyancyMode);
    forceCS.setFloat("u_TemperatureBuoyancyStrength", tempBounyancyStrength);
    forceCS.setFloat("u_DensityLow", densityLow);
    forceCS.setFloat("u_DensityHigh", densityHigh);
    forceCS.setFloat("u_BaroclinicStrength", BaroclinicStrength);

    // Tick vacuum timer
    if (vacuum.active) {
        vacuum.elapsed += dt;
        if (vacuum.elapsed >= vacuum.duration) {
            vacuum.active  = false;
            vacuum.elapsed = 0.0f;
        }
    }

    // Upload vacuum uniforms (always; shader gates on u_VacuumActive)
    forceCS.setInt  ("u_VacuumActive",   vacuum.active ? 1 : 0);
    forceCS.setVec3 ("u_VacuumWorldPos", vacuum.worldPos);
    forceCS.setFloat("u_VacuumStrength", vacuum.strength);
    forceCS.setFloat("u_VacuumRadius",   vacuum.radius);
    forceCS.setVec3 ("u_BoundsMin",      domain.boundsMin);
    forceCS.setFloat("u_VoxelSize",      domain.voxelSize);

    forceCS.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ApplyForces::destroy() {
    if (forceCS.ID) {
        glDeleteProgram(forceCS.ID);
        forceCS.ID = 0;
    }
}