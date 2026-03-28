#pragma once

#include "SmokeSolver/AdvectSmoke.h"
#include "SmokeSolver/AdvectVelocity.h"
#include "SmokeSolver/ComputeDivergence.h"
#include "SmokeSolver/PressureJacobi.h"
#include "SmokeSolver/ProjectVelocity.h"
#include "SmokeSolver/ApplyForces.h"
#include "core/smokeField.h"
#include "core/Buffer.h"

static constexpr int DEFAULT_ITER_COUNT = 60;
static constexpr float DEFAULT_VISCOSITY = 0.001f;
static constexpr float DEFAULT_DIFFUSION = 0.001f;

class SmokeSolver {
public:
    int pressureIterations = DEFAULT_ITER_COUNT;

    void init();
    void step(SmokeField& smoke, const SSBOBuffer& wallBuf, float dt);
    void destroy();

private:
    ApplyForces applyForces_;
    AdvectVelocity advectVelocity_;
    AdvectSmoke advectSmoke_;
    ComputeDivergence computeDivergence_;
    PressureJacobi pressureJacobi_;
    ProjectVelocity projectVelocity_;
};