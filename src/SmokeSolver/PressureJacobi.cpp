#include "SmokeSolver/PressureJacobi.h"

void PressureJacobi::init() {
    shader_.setUpFromFile("shaders/smoke/PressureJacobi.comp");
}

void PressureJacobi::iterate(const VoxelDomain& domain,
                           const SSBOBuffer& srcPressureBuf,
                           SSBOBuffer& destPressureBuf,
                           const SSBOBuffer& wallBuf,
                           const SSBOBuffer& divergenceBuf) {

    // 0 -> pressureSrc, 1 -> walls, 2 -> divergence, 3 -> pressureDest
    srcPressureBuf.bindBase(0);
    wallBuf.bindBase(1);
    divergenceBuf.bindBase(2);
    destPressureBuf.bindBase(3);

    shader_.use();
    shader_.setIVec3("u_GridSize", domain.gridSize);
    shader_.setFloat("u_CellSize", domain.voxelSize);
    shader_.setInt  ("u_VacuumActive",   vacuumActive);
    shader_.setVec3 ("u_VacuumWorldPos", vacuumWorldPos);
    shader_.setFloat("u_VacuumPressure", vacuumPressure);
    shader_.setVec3 ("u_BoundsMin",      domain.boundsMin);
    shader_.setFloat("u_VoxelSize",      domain.voxelSize);

    shader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void PressureJacobi::destroy() {
    if (shader_.ID != 0) {
        glDeleteProgram(shader_.ID);
        shader_.ID = 0;
    }
}