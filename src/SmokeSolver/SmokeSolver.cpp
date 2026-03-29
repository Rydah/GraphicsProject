#include "SmokeSolver/SmokeSolver.h"

void SmokeSolver::init() {
    applyForces_.init();
    advectSmoke_.init();
    advectVelocity_.init();
    computeDivergence_.init();
    pressureJacobi_.init();
    projectVelocity_.init();
}

void SmokeSolver::step(SmokeField& smoke, const SSBOBuffer& wallBuf, float dt) {

    if (dt <= 0.0f) return;

    // Optional for bug fixing (we do not consider the previous pressure)
    // smoke.pressure1.clear();
    // smoke.pressure2.clear();
    // smoke.pressure1Curr = true;
    // Comment out when we want to try use the previous values for faster convergence

    // advect velocity
    advectVelocity_.iterate(
        smoke.domain,
        smoke.getSrcVelocity(),
        smoke.getDestVelocity(),
        wallBuf,
        dt
    );
    smoke.swapVelocity();

    // apply forces
    applyForces_.dispatch(
        smoke.domain,
        smoke.getSrcVelocity(),
        smoke.getDestVelocity(),
        smoke.getSrcDensity(),
        wallBuf,
        dt
    );
    smoke.swapVelocity();
    
    // compute divergence
    computeDivergence_.run(
        smoke.domain,
        smoke.getSrcVelocity(),
        wallBuf,
        smoke.divergence
    );

    // pressure solve
    for (int i=0; i < pressureIterations; i++) {
        pressureJacobi_.iterate(
            smoke.domain,
            smoke.getSrcPressure(),
            smoke.getDestPressure(),
            wallBuf,
            smoke.divergence
        );
        smoke.swapPressure();
    }
    
    // project velocity
    projectVelocity_.iterate(
        smoke.domain,
        smoke.getSrcPressure(),
        smoke.getSrcVelocity(),
        smoke.getDestVelocity(),
        wallBuf,
        dt
    );
    smoke.swapVelocity();

    // advect smoke (we can actually choose to advect the smoke or velocity first or later as it doesnt matter visually)
    advectSmoke_.iterate(
        smoke.domain,
        smoke.getSrcVelocity(),
        smoke.getSrcDensity(),
        smoke.getDestDensity(),
        wallBuf,
        dt
    );
    smoke.swapDensity();
    


}

void SmokeSolver::destroy() {
    advectSmoke_.destroy();
    advectVelocity_.destroy();
    computeDivergence_.destroy();
    pressureJacobi_.destroy();
    projectVelocity_.destroy();
}