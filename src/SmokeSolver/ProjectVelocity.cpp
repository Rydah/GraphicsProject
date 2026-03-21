#include "SmokeSolver/ProjectVelocity.h"

void ProjectVelocity::init() {
    shader_.setUpFromFile("shaders/smoke/ProjectVelocity.comp");
}

void ProjectVelocity::iterate(const VoxelDomain& domain,
                              const SSBOBuffer& pressureBuf,
                              const SSBOBuffer& srcVelocityBuf,
                              SSBOBuffer& destVelocityBuf,
                              const SSBOBuffer& wallBuf,
                              float dt) {

    // 0 -> pressure, 1 -> walls, 2 -> srcVelocity, 3 -> destVelocity
    pressureBuf.bindBase(0);
    wallBuf.bindBase(1);
    srcVelocityBuf.bindBase(2);
    destVelocityBuf.bindBase(3);

    shader_.use();
    shader_.setIVec3("u_GridSize", domain.gridSize);
    shader_.setFloat("u_CellSize", domain.voxelSize);
    shader_.setFloat("u_Dt", dt);

    shader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ProjectVelocity::destroy() {
    if (shader_.ID != 0) {
        glDeleteProgram(shader_.ID);
        shader_.ID = 0;
    }
}