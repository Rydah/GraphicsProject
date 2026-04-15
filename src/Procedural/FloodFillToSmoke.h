#pragma once

#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

constexpr float DEFAULT_VELOCITY_INJECT_STRENGTH = 0.1f;
constexpr float DEFAULT_SMOKEDENSE_INJECT_STRENGTH = 0.8f;
constexpr float DEFAULT_SMOKETEMP_INJECT_STRENTH = 30.0f;

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
        int floodFillRadius,
        const VoxelDomain& domain,
        const glm::ivec3& seedCoord,
        const SSBOBuffer& srcSmokeDensityBuf,
        SSBOBuffer& destSmokeDensityBuf,
        const SSBOBuffer& wallBuf
    );
    void injectVelocity(
        const SSBOBuffer& floodFillBuf,
        int floodFillMaxValue,
        int floodFillRadius,
        const VoxelDomain& domain,
        const glm::ivec3& seedCoord,
        const SSBOBuffer& srcVelocityBuf,
        SSBOBuffer& destVelocityBuf,
        const SSBOBuffer& wallBuf
    );
    void injectAll(
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
    );
    void destroy();

    // Tunable parameters to proceduralsmokesystem
    float velocityInjectStrength_ = DEFAULT_VELOCITY_INJECT_STRENGTH;
    float smokeDenseInjectStrength_ = DEFAULT_SMOKEDENSE_INJECT_STRENGTH;
    float tempInjectStrenth_ = DEFAULT_SMOKETEMP_INJECT_STRENTH;
private:
    ComputeShader smokeFillShader_;
    ComputeShader velocityFillShader_;
};