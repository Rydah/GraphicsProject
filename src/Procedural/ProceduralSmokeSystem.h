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
    void setFloodFillStepsPerFrame(int steps) { floodFillStepsPerFrame_ = steps; }
    void setFloodFillVelocityInjectStrength(float strength) { floodFillToSmoke_.setVelocityInjectStrength(strength);}
    void setFloodFillSmokeInjectStrength(float strength) { floodFillToSmoke_.setSmokeDenseInjectStrength(strength);}

    // Add in other tunable parameters here

private:
    FloodFillToSmoke floodFillToSmoke_;
    int floodFillStepsPerFrame_ = 3;
    // we can add in the rest of tunable parameters here e.g
    // smokesolver iters
    // floodfill smoke source dissipation factor
    // floodfill effective max density etc.
};