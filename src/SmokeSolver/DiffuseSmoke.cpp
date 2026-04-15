#include "SmokeSolver/DiffuseSmoke.h"

void DiffuseSmoke::init() {
    shader_.setUpFromFile("shaders/smoke/DiffuseSmoke.comp");
}

void DiffuseSmoke::iterate(const VoxelDomain& domain,
                             const SSBOBuffer& srcSmokeDensityBuf,
                             SSBOBuffer& destSmokeDensityBuf,
                             const SSBOBuffer& wallBuf,
                             float dt) {
    // 0 -> walls
    // 1 -> source density
    // 2 -> dest density

    wallBuf.bindBase(0);
    srcSmokeDensityBuf.bindBase(1);
    destSmokeDensityBuf.bindBase(2);

    shader_.use();
    shader_.setIVec3("u_GridSize", domain.gridSize);
    shader_.setFloat("u_CellSize", domain.voxelSize);
    shader_.setFloat("u_SmokeDiffuseRate", smokeDiffuseRate_);
    shader_.setFloat("u_Dt", dt);

    shader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void DiffuseSmoke::destroy() {
    if (shader_.ID != 0) {
        glDeleteProgram(shader_.ID);
        shader_.ID = 0;
    }
}