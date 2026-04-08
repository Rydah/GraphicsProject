#include "FloodFillToSmoke.h"

void FloodFillToSmoke::init() {
    smokeFillShader_.setUpFromFile("shaders/smoke/FloodFillToSmoke.comp");
    velocityFillShader_.setUpFromFile("shaders/smoke/FloodFillToVelocity.comp");
}

void FloodFillToSmoke::injectSmoke(
        const SSBOBuffer& floodFillBuf,
        int floodFillRadius,
        const VoxelDomain& domain,
        const glm::ivec3& seedCoord,
        const SSBOBuffer& srcSmokeDensityBuf,
        SSBOBuffer& destSmokeDensityBuf,
        const SSBOBuffer& wallBuf
    ) {
    // 0 -> flood fill int src
    // 1 -> walls
    // 2 -> src smoke density 
    // 3 -> dest smoke density
    floodFillBuf.bindBase(0);
    wallBuf.bindBase(1);
    srcSmokeDensityBuf.bindBase(2);
    destSmokeDensityBuf.bindBase(3);

    smokeFillShader_.use();
    smokeFillShader_.setIVec3("u_GridSize", domain.gridSize);
    smokeFillShader_.setInt("u_FloodFillRadius", floodFillRadius);
    smokeFillShader_.setIVec3("u_SeedCoord", seedCoord);
    smokeFillShader_.setFloat("u_InjectStrength", smokeDenseInjectStrength_);

    smokeFillShader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// NOTE: Velocity injection is currently ADDITIVE and applied every frame while
// the flood fill is active. This means the source behaves like a continuous
// emitter (e.g., smoke grenade), not a one-shot explosion impulse.
//
// Effect:
// - Velocity keeps increasing in the source region over time
// - Can lead to overly strong / unrealistic expansion if left unchecked
//
// Future improvement:
// - Convert to one-shot impulse (inject only on newly activated voxels), OR
// - Apply time-based decay / dissipation to injected velocity, OR
// - Gate injection based on flood fill growth phase
//
// If smoke appears "too explosive", this is the first place to revisit.
void FloodFillToSmoke::injectVelocity(
        const SSBOBuffer& floodFillBuf,
        int floodFillMaxValue,
        int floodFillRadius,
        const VoxelDomain& domain,
        const glm::ivec3& seedCoord,
        const SSBOBuffer& srcVelocityBuf,
        SSBOBuffer& destVelocityBuf,
        const SSBOBuffer& wallBuf
    ) {
    // 0 -> flood fill int src
    // 1 -> walls
    // 2 -> src velocity  
    // 3 -> dest velocity
    floodFillBuf.bindBase(0);
    wallBuf.bindBase(1);
    srcVelocityBuf.bindBase(2);
    destVelocityBuf.bindBase(3);

    velocityFillShader_.use();
    velocityFillShader_.setIVec3("u_GridSize", domain.gridSize);
    velocityFillShader_.setInt("u_FloodFillMaxValue", floodFillMaxValue);
    velocityFillShader_.setInt("u_FloodFillRadius", floodFillRadius);
    velocityFillShader_.setIVec3("u_SeedCoord", seedCoord);
    velocityFillShader_.setFloat("u_InjectStrength", velocityInjectStrength_);
    velocityFillShader_.setFloat("u_TempInjectStrength", tempInjectStrenth_);

    velocityFillShader_.dispatch(domain.gridSize.x, domain.gridSize.y, domain.gridSize.z);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void FloodFillToSmoke::injectAll(
        const SSBOBuffer& floodFillBuf,
        int floodFillMaxValue,
        int floodFillRadius,
        const VoxelDomain& domain,
        const SSBOBuffer& srcSmokeDensityBuf,
        SSBOBuffer& destSmokeDensityBuf,
        const glm::ivec3& seedCoord,
        const SSBOBuffer& srcVelocityBuf,
        SSBOBuffer& destVelocityBuf,
        const SSBOBuffer& wallBuf,
        float elapsedTime
    ) {
    injectSmoke(
        floodFillBuf,
        floodFillRadius,
        domain,
        seedCoord,
        srcSmokeDensityBuf,
        destSmokeDensityBuf,
        wallBuf
    );
    if (elapsedTime < 2.5f) {
        injectVelocity(
            floodFillBuf,
            floodFillMaxValue,
            floodFillRadius,
            domain,
            seedCoord,
            srcVelocityBuf,
            destVelocityBuf,
            wallBuf
        );
    }
}

void FloodFillToSmoke::destroy() {
    if (smokeFillShader_.ID != 0) {
        glDeleteProgram(smokeFillShader_.ID);
        smokeFillShader_.ID = 0;
    }
    if (velocityFillShader_.ID != 0) {
        glDeleteProgram(velocityFillShader_.ID);
        velocityFillShader_.ID = 0;
    }
}