#pragma once

#include "Procedural/FloodFill.h"
#include "Procedural/FloodFillToSmoke.h"
#include "SmokeSolver/SmokeSolver.h"
#include "core/smokeField.h"
#include "core/Buffer.h"
#include "Voxel/VoxelDomain.h"


class ProceduralSmokeSystem {
public:
    void init();
    void update(
        VoxelFloodFill& floodFill,
        SmokeSolver& solver,
        SmokeField& smoke,
        const SSBOBuffer& wallBuf,
        const VoxelDomain& domain,
        float dt
    );
    void destroy();

    // Public setter methods to change the tunable parameters
    void setFloodFillStepsPerFrame(int steps) { floodFillStepsPerFrame_ = steps; } // num floodfill steps between fluid solving steps
    int getFloodFillStepsPerFrame() { return floodFillStepsPerFrame_; }
    void setFloodFillVelocityInjectStrength(float strength) { floodFillToSmoke_.velocityInjectStrength_ = strength;} // seeded smoke velocity
    float getFloodFillVelocityInjectStrength() { return floodFillToSmoke_.velocityInjectStrength_; }
    void setFloodFillSmokeInjectStrength(float strength) { floodFillToSmoke_.smokeDenseInjectStrength_ = strength;} // seeded smoke density
    float getFloodFillSmokeInjectStrength() { return floodFillToSmoke_.smokeDenseInjectStrength_; }
    // Add in other tunable parameters here

private:
    FloodFillToSmoke floodFillToSmoke_;
    int floodFillStepsPerFrame_ = 3;
    // we can add in the rest of tunable parameters here e.g
    // smokesolver iters
    // floodfill smoke source dissipation factor
    // floodfill effective max density etc.
};