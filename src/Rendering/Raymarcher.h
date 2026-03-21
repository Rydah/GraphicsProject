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
#include "glVersion.h"

// Volumetric ray marcher — Step 10a (Beer-Lambert core).
//
// Renders smoke at half resolution into an RGBA16F image via compute shader,
// then blits the result to screen with alpha-based compositing.
//
// Inputs:   smoke density SSBO, wall SSBO, scene depth texture
// Output:   smokeOut texture (RGB = scattered light, A = transmittance)
class Raymarcher {
public:
    Texture2D smokeOut;          // half-res output (RGBA16F)

    // Tweakable parameters
    float densityScale = 2.5f;
    float sigmaS       = 1.0f;  // scattering coefficient
    float sigmaA       = 0.05f; // absorption coefficient (smoke scatters, barely absorbs)
    int   phaseMode    = 0;     // 0 = Henyey-Greenstein, 1 = Rayleigh
    float gAsymmetry   = 0.4f;  // HG asymmetry; 0=isotropic, 0.4=mild forward scatter
    glm::vec3 lightDir   = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    glm::vec3 lightColor = glm::vec3(1.0f, 0.95f, 0.9f);
    float edgeFadeWidth  = 0.3f;
    float curlStrength   = 1.8f;
    float noiseStrength  = 0.7f;

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

    // Dispatch the ray-march compute shader.
    // Call after flood fill + solver have finished.
    // void render(const SSBOBuffer& smokeBuf,
    //             const SSBOBuffer& wallBuf,
    //             const Texture2D&  depthTex,
    //             const Texture3D&  noiseTex,
    //             const VoxelDomain& domain,
    //             const glm::mat4& view,
    //             const glm::mat4& proj,
    //             float zNear, float zFar,
    //             int maxDensityVal,
    //             float timeSec,
    //             glm::vec3 seedWorldPos,
    //             int maxSeedVal,
    //             float radiusXZ,
    //             float radiusY)
    // {
    void render(const SSBOBuffer& smokeBuf,
            const SSBOBuffer& wallBuf,
            const Texture2D&  depthTex,
            const Texture3D&  noiseTex,
            const VoxelDomain& domain,
            const glm::mat4& view,
            const glm::mat4& proj,
            float zNear, float zFar,
            float timeSec)
    {
        glm::mat4 invView = glm::inverse(view);
        glm::mat4 invProj = glm::inverse(proj);

        // Bind output image
        smokeOut.bindImage(0, GL_WRITE_ONLY);

        // Bind depth texture as sampler on unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, depthTex.ID);

        // Bind Worley noise volume as sampler on unit 1
        noiseTex.bindSampler(1);

        // Bind SSBOs
        smokeBuf.bindBase(0);
        wallBuf.bindBase(1);

        marchCS.use();
        marchCS.setMat4 ("u_InvView",       invView);
        marchCS.setMat4 ("u_InvProj",       invProj);
        marchCS.setFloat("u_Near",           zNear);
        marchCS.setFloat("u_Far",            zFar);
        marchCS.setIVec3("u_GridSize",       domain.gridSize);
        marchCS.setVec3 ("u_BoundsMin",      domain.boundsMin);
        marchCS.setVec3 ("u_BoundsMax",      domain.boundsMax);
        marchCS.setFloat("u_VoxelSize",      domain.voxelSize);
        // marchCS.setInt  ("u_MaxDensityVal",  maxDensityVal); No longer used in shader
        marchCS.setFloat("u_DensityScale",   densityScale);
        marchCS.setFloat("u_SigmaS",         sigmaS);
        marchCS.setFloat("u_SigmaA",         sigmaA);
        marchCS.setInt  ("u_PhaseMode",      phaseMode);
        marchCS.setFloat("u_G",              glm::clamp(gAsymmetry, -0.999f, 0.999f));
        marchCS.setVec3 ("u_LightDir",       lightDir);
        marchCS.setVec3 ("u_LightColor",     lightColor);
        marchCS.setFloat("u_Time",           timeSec);
        marchCS.setFloat("u_EdgeFadeWidth",  edgeFadeWidth);
        marchCS.setFloat("u_CurlStrength",   curlStrength);
        marchCS.setFloat("u_NoiseStrength",  noiseStrength);
        // marchCS.setVec3 ("u_SeedWorldPos",   seedWorldPos); No longer used in shader
        // marchCS.setInt  ("u_MaxSeedVal",     maxSeedVal);
        // marchCS.setFloat("u_RadiusXZ",       radiusXZ);
        // marchCS.setFloat("u_RadiusY",        radiusY);

        // ivec2 for texture size
        glUniform2i(glGetUniformLocation(marchCS.ID, "u_TexSize"), halfW, halfH);

        // sampler binding
        marchCS.setInt("u_DepthTex", 0);
        marchCS.setInt("u_NoiseTex", 1);

        marchCS.dispatch(halfW, halfH, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    // Draw the smoke result composited over the current framebuffer content.
    // Call this after rendering the scene (e.g. voxel debug view).
    void blit(FullscreenQuad& quad) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_SRC_ALPHA);
        // final = smokeRGB * 1  +  scene * transmittance
        // (transmittance is in alpha channel)

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
        if (marchCS.ID)   { glDeleteProgram(marchCS.ID);   marchCS.ID = 0; }
        if (blitShader.ID) { glDeleteProgram(blitShader.ID); blitShader.ID = 0; }
    }

private:
    ComputeShader marchCS;
    shader        blitShader;
    int halfW = 0, halfH = 0;

    void buildBlitShader() {
        // Full-screen quad pass that composites smoke over the scene.
        // Uses pre-multiplied alpha blend:
        //   result = smokeRGB + scene * transmittance
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
            "    // RGB = accumulated scattered light, A = transmittance\n"
            "    FragColor = vec4(smoke.rgb, smoke.a);\n"
            "}\n";

        blitShader.setUpShader(vs, fs);
    }
};
