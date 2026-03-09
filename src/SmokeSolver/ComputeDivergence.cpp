#include "ComputeDivergence.h"

void ComputeDivergence::init() {
    shader_.setUpFromFile("shaders/smoke/ComputeDivergence.comp");
}

void ComputeDivergence::run(const VoxelDomain& domain,
                              const SSBOBuffer& velocityBuf,
                              const SSBOBuffer& wallBuf,
                              const SSBOBuffer& divergenceBuf) {
    // 0 -> velocity, 1 -> walls, 2 -> divergence
    velocityBuf.bindBase(0);
    wallBuf.bindBase(1);
    divergenceBuf.bindBase(2);

    shader_.use();
    shader_.setIVec3("u_GridSize", domain.gridSize);
    shader_.setFloat("u_CellSize", domain.voxelSize);

    shader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ComputeDivergence::destroy() {
    if (shader_.ID != 0) {
        glDeleteProgram(shader_.ID);
        shader_.ID = 0;
    }
}