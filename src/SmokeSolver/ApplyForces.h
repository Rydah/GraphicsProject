#include "core/Buffer.h"
#include "core/ComputeShader.h"
#include "Voxel/VoxelDomain.h"

class ApplyForces {
public:
    float buoyancyStrength = 1.0f;
    float gravityStrength  = 0.05f;

    void init();
    void dispatch(
                const VoxelDomain& domain,
                const SSBOBuffer& velocitySrc,
                const SSBOBuffer& velocityDst,
                const SSBOBuffer& smokeBuf,
                const SSBOBuffer& wallBuf,
                float dt);

    void destroy();

private:
    ComputeShader forceCS;
};