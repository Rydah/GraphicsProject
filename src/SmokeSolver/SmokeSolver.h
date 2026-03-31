#pragma once

#include "SmokeSolver/DiffuseSmoke.h"
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
    int  pressureIterations = DEFAULT_ITER_COUNT;
    bool advectSmokeEnabled = true;

    void init();
    void step(SmokeField& smoke, const SSBOBuffer& wallBuf, float dt);
    void destroy();

    // Sets the buoyancy force strength (0.0 - 2.0f)
    void setBuoyancy(float buoyancy) {
        applyForces_.buoyancyStrength = buoyancy; 
    }

    float getBuoyancy() {
        return applyForces_.buoyancyStrength; 
    }

    // Sets the gravity force strength (0.0 - 2.0f)
    void setGravity(float gravity) {
        applyForces_.gravityStrength = gravity;
    }

    float getGravity() {
        return applyForces_.gravityStrength; 
    }

    // Sets Smoke falloff (0.0 - 1.0f)
    void setSmokeFallOff(float smokeFallOff) {
        advectSmoke_.smokeFallOff = smokeFallOff;
    }

    float getSmokeFallOff() {
        return advectSmoke_.smokeFallOff; 
    }

    // Sets Smoke Diffusion rate (0.0 - 0.1f)
    void setSmokeDiffsionRate(float smokeDiffusionRate) {
        diffuseSmoke_.setSmokeDiffuseRate(smokeDiffusionRate);
    }

    float getSmokeDiffusionRate() {
        return diffuseSmoke_.getSmokeDiffuseRate(); 
    }

private:
    ApplyForces applyForces_;
    AdvectVelocity advectVelocity_;
    AdvectSmoke advectSmoke_;
    ComputeDivergence computeDivergence_;
    PressureJacobi pressureJacobi_;
    ProjectVelocity projectVelocity_;
    DiffuseSmoke diffuseSmoke_;
};