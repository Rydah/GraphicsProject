#ifndef SELF_TESTS_H
#define SELF_TESTS_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <cmath>

#include "ComputeShader.h"
#include "Buffer.h"
#include "Texture3D.h"

// GPU self-tests run once at startup to verify the compute pipeline works.
// Call runAllTests() after GLAD is loaded and an OpenGL context is active.
namespace SelfTests {

// Verifies compute shaders can write to an SSBO and the CPU can read back.
inline bool testComputeSSBO() {
    const char* src =
        "#version 430 core\n"
        "layout(local_size_x = 64) in;\n"
        "layout(std430, binding = 0) buffer OutBuf { int data[]; };\n"
        "void main() {\n"
        "    uint i = gl_GlobalInvocationID.x;\n"
        "    data[i] = int(i * i);\n"
        "}\n";

    ComputeShader cs;
    cs.setUp(src);

    const int N = 256;
    SSBOBuffer buf;
    buf.allocate(N * sizeof(int));
    buf.clear();
    buf.bindBase(0);

    cs.dispatch(N);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    auto result = buf.download<int>(N);
    bool pass = true;
    for (int i = 0; i < N; i++)
        if (result[i] != i * i) { pass = false; break; }

    buf.destroy();
    glDeleteProgram(cs.ID);

    std::cout << "  Compute->SSBO:     " << (pass ? "PASSED" : "FAILED") << "\n";
    return pass;
}

// Verifies imageStore to a 3D texture and imageLoad readback are correct.
inline bool testTexture3DRoundTrip() {
    const char* writeSrc =
        "#version 430 core\n"
        "layout(local_size_x=8,local_size_y=8,local_size_z=8) in;\n"
        "layout(binding=0, r16f) uniform image3D u_Vol;\n"
        "void main() {\n"
        "    ivec3 c = ivec3(gl_GlobalInvocationID);\n"
        "    ivec3 s = imageSize(u_Vol);\n"
        "    if (any(greaterThanEqual(c,s))) return;\n"
        "    float v = float(c.x+c.y+c.z)/float(s.x+s.y+s.z);\n"
        "    imageStore(u_Vol, c, vec4(v));\n"
        "}\n";

    const char* readSrc =
        "#version 430 core\n"
        "layout(local_size_x=8,local_size_y=8,local_size_z=8) in;\n"
        "layout(binding=0, r16f) readonly uniform image3D u_Vol;\n"
        "layout(std430, binding=0) buffer OutBuf { float data[]; };\n"
        "uniform ivec3 u_Size;\n"
        "void main() {\n"
        "    ivec3 c = ivec3(gl_GlobalInvocationID);\n"
        "    if (any(greaterThanEqual(c,u_Size))) return;\n"
        "    int i = c.x + c.y*u_Size.x + c.z*u_Size.x*u_Size.y;\n"
        "    data[i] = imageLoad(u_Vol,c).r;\n"
        "}\n";

    const int SZ = 64;
    Texture3D tex;
    tex.create(SZ, SZ, SZ, GL_R16F);

    ComputeShader writeCS, readCS;
    writeCS.setUp(writeSrc);
    tex.bindImage(0, GL_WRITE_ONLY);
    writeCS.dispatch(SZ, SZ, SZ);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    readCS.setUp(readSrc);
    readCS.use();
    readCS.setIVec3("u_Size", glm::ivec3(SZ));

    SSBOBuffer buf;
    buf.allocate(SZ*SZ*SZ * sizeof(float));
    buf.bindBase(0);
    tex.bindImage(0, GL_READ_ONLY);
    readCS.dispatch(SZ, SZ, SZ);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    auto result = buf.download<float>(SZ*SZ*SZ);
    float maxSum = float(SZ + SZ + SZ);
    int checks[][3] = {{0,0,0},{1,2,3},{32,32,32},{63,63,63}};
    bool pass = true;
    for (auto& c : checks) {
        int idx = c[0] + c[1]*SZ + c[2]*SZ*SZ;
        float expected = float(c[0]+c[1]+c[2]) / maxSum;
        if (std::abs(result[idx] - expected) > 0.01f) { pass = false; break; }
    }

    buf.destroy();
    tex.destroy();
    glDeleteProgram(writeCS.ID);
    glDeleteProgram(readCS.ID);

    std::cout << "  Texture3D imageStore: " << (pass ? "PASSED" : "FAILED") << "\n";
    return pass;
}

inline void runAllTests() {
    std::cout << "[SelfTests]\n";
    testComputeSSBO();
    testTexture3DRoundTrip();
    std::cout << std::endl;
}

} // namespace SelfTests

#endif // SELF_TESTS_H
