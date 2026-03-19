#include "SmokeSolver/AdvectVelocity.h"

void AdvectVelocity::init() {
    shader_.setUpFromFile("shaders/smoke/AdvectVelocity.comp");
}

void AdvectVelocity::iterate(const VoxelDomain& domain,
                             const SSBOBuffer& srcVelocityBuf,
                             SSBOBuffer& destVelocityBuf,
                             const SSBOBuffer& wallBuf,
                             float dt) {
    // 0 -> source velocity
    // 1 -> walls
    // 2 -> destination velocity

    srcVelocityBuf.bindBase(0);
    wallBuf.bindBase(1);
    destVelocityBuf.bindBase(2);

    shader_.use();
    shader_.setIVec3("u_GridSize", domain.gridSize);
    shader_.setFloat("u_CellSize", domain.voxelSize);
    shader_.setVec3("u_BoundsMin", domain.boundsMin);
    shader_.setFloat("u_Dt", dt);

    shader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void AdvectVelocity::destroy() {
    if (shader_.ID != 0) {
        glDeleteProgram(shader_.ID);
        shader_.ID = 0;
    }
}