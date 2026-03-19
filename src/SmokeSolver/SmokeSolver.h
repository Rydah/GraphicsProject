#pragma once

#include "SmokeSolver/AdvectVelocity.h"
#include "SmokeSolver/ComputeDivergence.h"
#include "SmokeSolver/PressureJacobi.h"
#include "SmokeSolver/ProjectVelocity.h"
#include "core/smokeField.h"
#include "core/Buffer.h"

static constexpr int DEFAULT_ITER_COUNT = 30;
static constexpr float DEFAULT_VISCOSITY = 0.001f;
static constexpr float DEFAULT_DIFFUSION = 0.001f;

class SmokeSolver {
public:
    int pressureIterations = 30;

    void init();
    void step(SmokeField& smoke, const SSBOBuffer& wallBuf, float dt);
    void destroy();

private:
    AdvectVelocity advectVelocity_;
    ComputeDivergence computeDivergence_;
    PressureJacobi pressureJacobi_;
    ProjectVelocity projectVelocity_;
};