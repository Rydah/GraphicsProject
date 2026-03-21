#pragma once

#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

constexpr float DEFAULT_VELOCITY_INJECT_STRENGTH = 0.95;
constexpr float DEFAULT_SMOKEDENSE_INJECT_STRENGTH = 0.5f;

/* Wrapper class for injecting flood-fill-derived source terms into the smoke simulation.

   Supports:
   - smoke density injection into the smoke scalar buffers
   - velocity injection into the smoke velocity buffers

   Each injection is performed by a dedicated compute shader.
*/
class FloodFillToSmoke {
public:
    void init();
    void injectSmoke(
        const SSBOBuffer& floodFillBuf,
        int floodFillMaxValue,
        const VoxelDomain& domain,
        const SSBOBuffer& srcSmokeDensityBuf,
        SSBOBuffer& destSmokeDensityBuf,
        const SSBOBuffer& wallBuf,
        glm::ivec3 seedCoord,
        int maxSeedVal,
        float radiusXZ,
        float radiusY
    );
    void injectVelocity(
        const SSBOBuffer& floodFillBuf,
        int floodFillMaxValue,
        const VoxelDomain& domain,
        const glm::ivec3& seedCoord,
        int maxSeedVal,
        float radiusXZ,
        float radiusY,
        const SSBOBuffer& srcVelocityBuf,
        SSBOBuffer& destVelocityBuf,
        const SSBOBuffer& wallBuf
    );
    void injectAll(
        const SSBOBuffer& floodFillBuf,
        int floodFillMaxValue,
        const VoxelDomain& domain,
        const SSBOBuffer& srcSmokeDensityBuf,
        SSBOBuffer& destSmokeDensityBuf,
        const glm::ivec3& seedCoord,
        int maxSeedVal,
        float radiusXZ,
        float radiusY,
        const SSBOBuffer& srcVelocityBuf,
        SSBOBuffer& destVelocityBuf,
        const SSBOBuffer& wallBuf
    );
    void destroy();

    // setters to expose these parameters to proceduralsmokesystem
    void setVelocityInjectStrength(float strength) { velocityInjectStrength_ = strength; }
    void setSmokeDenseInjectStrength(float strength) { smokeDenseInjectStrength_ = strength; }
    // Fraction of ellipsoid radius below which NO velocity is injected (0=everywhere, ~0.6=surface only)
    void setShellInner(float inner) { shellInner_ = inner; }

private:
    ComputeShader smokeFillShader_;
    ComputeShader velocityFillShader_;
    float velocityInjectStrength_ = DEFAULT_VELOCITY_INJECT_STRENGTH;
    float smokeDenseInjectStrength_ = DEFAULT_SMOKEDENSE_INJECT_STRENGTH;
    float shellInner_ = 0.6f;
};