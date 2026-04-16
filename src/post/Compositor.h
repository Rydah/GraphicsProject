#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include "core/Texture2D.h"
#include "core/shader.h"
#include "core/FullscreenQuad.h"
#include "glVersion.h"

class Compositor {
public:
    // Debug modes
    enum DebugMode {
        DEBUG_FINAL      = 0,  // Final composited result
        DEBUG_SMOKE_ONLY = 1,  // Smoke RGB only
        DEBUG_DENSITY    = 2,  // Transmittance mask
        DEBUG_DEPTH      = 3   // Depth texture
    };
    
    float sharpenStrength = 0.5f;
    int debugMode = DEBUG_FINAL;
    
    void init() {
        buildCompositeShader();
    }
    
    // Composite smoke over scene with optional sharpening.
    // sceneColorTex: background scene rendered into an FBO (RGBA)
    // smokeTex:      upsampled smoke (RGBA, where A = transmittance from Beer-Lambert)
    // depthTex:      scene depth for debug mode
    //
    // IMPORTANT: sceneColorTex must be a real render of your scene geometry.
    // Create a Framebuffer with a GL_RGBA8 color attachment, render voxelDebug
    // (and any other opaque geometry) into it each frame, then pass that texture
    // here.  Passing an uninitialised / always-black texture makes the Final and
    // Smoke-Only debug modes look identical.
    // smokeTex: bilaterally-upsampled smoke (RGBA16F).
    //           A = transmittance — depth-aware so it correctly terminates at wall edges.
    void composite(const Texture2D& sceneColorTex,
                   const Texture2D& smokeTex,
                   const Texture2D& depthTex,
                   FullscreenQuad& quad) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, smokeTex.width, smokeTex.height);

        compositeShader.use();
        compositeShader.setInt("u_SceneTex",          0);
        compositeShader.setInt("u_SmokeTex",          1);
        compositeShader.setInt("u_DepthTex",          2);
        compositeShader.setFloat("u_SharpenStrength", sharpenStrength);
        glUniform1i(glGetUniformLocation(compositeShader.ID, "u_DebugMode"), debugMode);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneColorTex.ID);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, smokeTex.ID);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, depthTex.ID);

        glDisable(GL_DEPTH_TEST);
        quad.draw();
        glEnable(GL_DEPTH_TEST);
    }
    
    void destroy() {
        if (compositeShader.ID) {
            glDeleteProgram(compositeShader.ID);
            compositeShader.ID = 0;
        }
    }
    
private:
    shader compositeShader;
    
    void buildCompositeShader() {
        const char* vs = GLSL_VERSION
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 texCoord;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "    texCoord = aPos * 0.5 + 0.5;\n"
            "}\n";
        
        // KEY FIXES vs the original:
        //
        // 1. Sharpening is applied to RGB only.
        //    The Laplacian kernel is a high-frequency amplifier. Applying it to
        //    the transmittance (alpha) channel introduces ringing at smoke edges
        //    where alpha transitions sharply 0→1, which corrupts the blend mask
        //    and creates dark/bright halos. The alpha channel must stay as-is.
        //
        // 2. Compositing uses center.a (original transmittance) not sharpened.a.
        //    Beer-Lambert transmittance is the physically correct blend weight;
        //    sharpening it breaks energy conservation and causes edge artifacts.
        //
        // 3. debug mode 1 (SMOKE_ONLY) now shows smoke.rgb over black so it is
        //    visually distinct from the Final mode even when sceneColorTex is black.
        //    In production, sceneColorTex should be a real FBO render of your scene.
        const char* fs = GLSL_VERSION
            "in vec2 texCoord;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D u_SceneTex;\n"
            "uniform sampler2D u_SmokeTex;\n"  // RGBA16F — A = bilateral-upsampled transmittance
            "uniform sampler2D u_DepthTex;\n"
            "uniform float u_SharpenStrength;\n"
            "uniform int u_DebugMode;\n"
            "\n"
            "void main() {\n"
            "    vec4  scene         = texture(u_SceneTex, texCoord);\n"
            "    vec4  smoke         = texture(u_SmokeTex, texCoord);\n"
            "    // Bilateral upsampling already rejected cross-edge neighbours,\n"
            "    // so smoke.a terminates correctly at wall geometry.\n"
            "    float transmittance = clamp(smoke.a, 0.0, 1.0);\n"
            "    float depth         = texture(u_DepthTex, texCoord).r;\n"
            "    \n"
            "    // ---- Debug modes ------------------------------------------------\n"
            "    if (u_DebugMode == 1) { fragColor = vec4(smoke.rgb, 1.0); return; }\n"
            "    if (u_DebugMode == 2) { fragColor = vec4(vec3(1.0 - transmittance), 1.0); return; }\n"
            "    if (u_DebugMode == 3) { fragColor = vec4(vec3(depth), 1.0); return; }\n"
            "    \n"
            "    // ---- Sharpen smoke RGB, blend using depth-correct transmittance --\n"
            "    vec2 texelSize = 1.0 / textureSize(u_SmokeTex, 0);\n"
            "    vec3 n = texture(u_SmokeTex, texCoord + vec2(0.0,          texelSize.y)).rgb;\n"
            "    vec3 s = texture(u_SmokeTex, texCoord - vec2(0.0,          texelSize.y)).rgb;\n"
            "    vec3 e = texture(u_SmokeTex, texCoord + vec2(texelSize.x,  0.0        )).rgb;\n"
            "    vec3 w = texture(u_SmokeTex, texCoord - vec2(texelSize.x,  0.0        )).rgb;\n"
            "    float opacity  = 1.0 - transmittance;\n"
            "    float edgeFade = smoothstep(0.2, 0.8, opacity);\n"
            "    vec3 sharpened = clamp(\n"
            "        smoke.rgb + (4.0 * smoke.rgb - n - s - e - w) * (u_SharpenStrength * edgeFade),\n"
            "        0.0, 1.0);\n"
            "    \n"
            "    fragColor = vec4(mix(sharpened, scene.rgb, transmittance), 1.0);\n"
            "}\n";
        
        GLint vs_ok, fs_ok, prog_ok;

        uint32_t vs_id = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs_id, 1, &vs, nullptr);
        glCompileShader(vs_id);
        glGetShaderiv(vs_id, GL_COMPILE_STATUS, &vs_ok);
        if (!vs_ok) {
            char log[1024] = {};
            glGetShaderInfoLog(vs_id, 1024, nullptr, log);
            std::cerr << "Compositor VS compile error:\n" << log << std::endl;
        }
        
        uint32_t fs_id = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs_id, 1, &fs, nullptr);
        glCompileShader(fs_id);
        glGetShaderiv(fs_id, GL_COMPILE_STATUS, &fs_ok);
        if (!fs_ok) {
            char log[1024] = {};
            glGetShaderInfoLog(fs_id, 1024, nullptr, log);
            std::cerr << "Compositor FS compile error:\n" << log << std::endl;
        }
        
        compositeShader.ID = glCreateProgram();
        glAttachShader(compositeShader.ID, vs_id);
        glAttachShader(compositeShader.ID, fs_id);
        glLinkProgram(compositeShader.ID);
        glGetProgramiv(compositeShader.ID, GL_LINK_STATUS, &prog_ok);
        if (!prog_ok) {
            char log[1024] = {};
            glGetProgramInfoLog(compositeShader.ID, 1024, nullptr, log);
            std::cerr << "Compositor shader link error:\n" << log << std::endl;
        }
        
        glDeleteShader(vs_id);
        glDeleteShader(fs_id);
    }
};