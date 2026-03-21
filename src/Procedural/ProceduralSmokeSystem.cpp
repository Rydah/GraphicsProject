#include "ProceduralSmokeSystem.h"

void ProceduralSmokeSystem::init() {
    floodFillToSmoke_.init();
}

void ProceduralSmokeSystem::update(
        VoxelFloodFill& floodFill,
        SmokeSolver& solver,
        SmokeField& smoke,
        const SSBOBuffer& wallBuf,
        const VoxelDomain& domain,
        float dt
    ) {
    
    if (dt <= 0.0f) return;

    // Step 1: Expand and seed the smoke source region
    // We might have to modify floodfill to accept a tunable dissipation factor so the "smoke" eventually stops being generated
    floodFill.propagate(
        floodFillStepsPerFrame_,
        domain.gridSize,
        domain.boundsMin,
        domain.voxelSize,
        wallBuf,
        dt
    );

    // Step 2: Inject floodfill source into smoke scalar field (Density buffer)
    floodFillToSmoke_.injectAll(
        floodFill.currentBuffer(),
        floodFill.effectiveMaxDensity(),
        domain,
        smoke.getSrcDensity(),
        smoke.getDestDensity(),
        floodFill.seedCoord,
        floodFill.maxSeedValue,
        floodFill.radiusXZ,
        floodFill.radiusY,
        smoke.getSrcVelocity(),
        smoke.getDestVelocity(),
        wallBuf
    );
    smoke.swapVelocity();
    smoke.swapDensity();

    // Step 3: Run the smoke solver on the newly injected smoke state
    // Again we will add a tunable parameter for the number of solve iterations and any other things we might want to change
    solver.step(smoke, wallBuf, dt);

}

void ProceduralSmokeSystem::destroy() {
    floodFillToSmoke_.destroy();
}