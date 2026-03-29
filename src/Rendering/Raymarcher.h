#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/ComputeShader.h"
#include "core/Buffer.h"
#include "core/Texture2D.h"
#include "core/Texture3D.h"
#include "core/FullscreenQuad.h"
#include "core/shader.h"
#include "Voxel/VoxelDomain.h"
#include "Rendering/LightSource.h"
#include "glVersion.h"

// Volumetric ray marcher.
//
// Renders smoke at half resolution into an RGBA16F image via compute shader,
// then blits the result to screen with alpha-based compositing.
//
// Inputs:   smoke density SSBO, wall SSBO, scene depth texture
// Output:   smokeOut texture (RGB = scattered light, A = transmittance)
class Raymarcher {
public:
    Texture2D smokeOut;

    // Tweakable parameters
    float densityScale = 2.0f;

    float sigmaS = 0.3f;   // scattering
    float sigmaA = 0.5f;   // absorption

    float phaseBlend = 0.5f;    // 0 = Henyey-Greenstein, 1 = Rayleigh
    float g = 0.20f;

    float edgeFadeWidth  = 0.3f;
    float curlStrength   = 1.0f;
    float noiseStrength  = 0.75f;
    float noiseScale     = 3.0f;

    void init(int fullWidth, int fullHeight) {
        halfW = fullWidth  / 2;
        halfH = fullHeight / 2;

        smokeOut.create(halfW, halfH, GL_RGBA16F);

        marchCS.setUpFromFile("shaders/smoke/Raymarch.comp");
        buildBlitShader();
    }

    void resize(int fullWidth, int fullHeight) {
        int newW = fullWidth  / 2;
        int newH = fullHeight / 2;
        if (newW == halfW && newH == halfH) return;
        halfW = newW;
        halfH = newH;
        smokeOut.destroy();
        smokeOut.create(halfW, halfH, GL_RGBA16F);
    }

    void render(const SSBOBuffer& smokeBuf,
            const SSBOBuffer& wallBuf,
            const Texture2D&  depthTex,
            const Texture3D&  noiseTex,
            const VoxelDomain& domain,
            const glm::mat4& view,
            const glm::mat4& proj,
            float zNear, float zFar,
            float timeSec,
            const LightSource& light)
    {
        glm::mat4 invView = glm::inverse(view);
        glm::mat4 invProj = glm::inverse(proj);

        smokeOut.bindImage(0, GL_WRITE_ONLY);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, depthTex.ID);

        noiseTex.bindSampler(1);

        smokeBuf.bindBase(0);
        wallBuf.bindBase(1);

        marchCS.use();
        marchCS.setMat4 ("u_InvView",      invView);
        marchCS.setMat4 ("u_InvProj",      invProj);
        marchCS.setFloat("u_Near",         zNear);
        marchCS.setFloat("u_Far",          zFar);
        marchCS.setIVec3("u_GridSize",     domain.gridSize);
        marchCS.setVec3 ("u_BoundsMin",    domain.boundsMin);
        marchCS.setVec3 ("u_BoundsMax",    domain.boundsMax);
        marchCS.setFloat("u_VoxelSize",    domain.voxelSize);

        marchCS.setFloat("u_DensityScale", densityScale);
        marchCS.setFloat("u_SigmaS",       sigmaS);
        marchCS.setFloat("u_SigmaA",       sigmaA);
        marchCS.setFloat("u_PhaseBlend",   phaseBlend);
        marchCS.setFloat("u_G",            g);

        marchCS.setVec3 ("u_LightDir",     light.getDirection());
        marchCS.setVec3 ("u_LightColor",   light.getColor());
        marchCS.setFloat("u_Time",         timeSec);

        marchCS.setFloat("u_EdgeFadeWidth", edgeFadeWidth);
        marchCS.setFloat("u_CurlStrength",  curlStrength);
        marchCS.setFloat("u_NoiseStrength", noiseStrength);
        marchCS.setFloat("u_NoiseScale",    noiseScale);

        glUniform2i(glGetUniformLocation(marchCS.ID, "u_TexSize"), halfW, halfH);

        marchCS.setInt("u_DepthTex", 0);
        marchCS.setInt("u_NoiseTex", 1);

        marchCS.dispatch(halfW, halfH, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    void blit(FullscreenQuad& quad) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_SRC_ALPHA);

        blitShader.use();
        blitShader.setInt("u_SmokeTex", 0);
        smokeOut.bindSampler(0);

        glDisable(GL_DEPTH_TEST);
        quad.draw();
        glEnable(GL_DEPTH_TEST);

        glDisable(GL_BLEND);
    }

    void destroy() {
        smokeOut.destroy();
        if (marchCS.ID)    { glDeleteProgram(marchCS.ID);    marchCS.ID = 0; }
        if (blitShader.ID) { glDeleteProgram(blitShader.ID); blitShader.ID = 0; }
    }

private:
    ComputeShader marchCS;
    shader        blitShader;
    int halfW = 0, halfH = 0;

    void buildBlitShader() {
        const char* vs = GLSL_VERSION
            "layout(location=0) in vec2 aPos;\n"
            "layout(location=1) in vec2 aUV;\n"
            "out vec2 vUV;\n"
            "void main() { gl_Position = vec4(aPos,0,1); vUV = aUV; }\n";

        const char* fs = GLSL_VERSION
            "in vec2 vUV;\n"
            "out vec4 FragColor;\n"
            "uniform sampler2D u_SmokeTex;\n"
            "void main() {\n"
            "    vec4 smoke = texture(u_SmokeTex, vUV);\n"
            "    FragColor = vec4(smoke.rgb, smoke.a);\n"
            "}\n";

        blitShader.setUpShader(vs, fs);
    }
};