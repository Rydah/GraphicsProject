#include "SmokeSolver/AdvectSmoke.h"

void AdvectSmoke::init() {
    shader_.setUpFromFile("shaders/smoke/AdvectSmoke.comp");
}

void AdvectSmoke::iterate(const VoxelDomain& domain,
                             const SSBOBuffer& srcVelocityBuf,
                             const SSBOBuffer& srcSmokeDensityBuf,
                             SSBOBuffer& destSmokeDensityBuf,
                             const SSBOBuffer& wallBuf,
                             float dt) {
    // 0 -> source velocity
    // 1 -> walls
    // 2 -> source density
    // 3 -> dest density

    srcVelocityBuf.bindBase(0);
    wallBuf.bindBase(1);
    srcSmokeDensityBuf.bindBase(2);
    destSmokeDensityBuf.bindBase(3);

    shader_.use();
    shader_.setIVec3("u_GridSize", domain.gridSize);
    shader_.setFloat("u_CellSize", domain.voxelSize);
    shader_.setVec3("u_BoundsMin", domain.boundsMin);
    shader_.setFloat("u_Dt", dt);

    shader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void AdvectSmoke::destroy() {
    if (shader_.ID != 0) {
        glDeleteProgram(shader_.ID);
        shader_.ID = 0;
    }
}