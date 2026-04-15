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
    void composite(const Texture2D& sceneColorTex,
                   const Texture2D& smokeTex,
                   const Texture2D& depthTex,
                   FullscreenQuad& quad) {
        // Render to default framebuffer (screen)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, smokeTex.width, smokeTex.height);
        
        compositeShader.use();
        compositeShader.setInt("u_SceneTex",        0);
        compositeShader.setInt("u_SmokeTex",        1);
        compositeShader.setInt("u_DepthTex",        2);
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
            "uniform sampler2D u_SmokeTex;\n"
            "uniform sampler2D u_DepthTex;\n"
            "uniform float u_SharpenStrength;\n"
            "uniform int u_DebugMode;\n"
            "\n"
            "void main() {\n"
            "    vec4 scene = texture(u_SceneTex, texCoord);\n"
            "    vec4 smoke = texture(u_SmokeTex, texCoord);\n"
            "    float depth = texture(u_DepthTex, texCoord).r;\n"
            "    \n"
            "    // ---- Debug modes ------------------------------------------------\n"
            "    if (u_DebugMode == 1) {\n"
            "        // Smoke RGB over black — lets you inspect colour independently\n"
            "        // of whether sceneColorTex is wired up.\n"
            "        fragColor = vec4(smoke.rgb, 1.0);\n"
            "        return;\n"
            "    }\n"
            "    else if (u_DebugMode == 2) {\n"
            "        // Density / opacity visualisation (1 - transmittance)\n"
            "        // White = fully opaque smoke, black = clear air.\n"
            "        float density = 1.0 - smoke.a;\n"
            "        fragColor = vec4(vec3(density), 1.0);\n"
            "        return;\n"
            "    }\n"
            "    else if (u_DebugMode == 3) {\n"
            "        // Raw depth buffer visualisation\n"
            "        fragColor = vec4(vec3(depth), 1.0);\n"
            "        return;\n"
            "    }\n"
            "    \n"
            "    // ---- Default: sharpen RGB then composite -------------------------\n"
            "    vec2 texelSize = 1.0 / textureSize(u_SmokeTex, 0);\n"
            "    \n"
            "    // Sample the 4-neighbourhood for the Laplacian kernel.\n"
            "    vec4 center = texture(u_SmokeTex, texCoord);\n"
            "    vec4 north  = texture(u_SmokeTex, texCoord + vec2(0.0,        texelSize.y));\n"
            "    vec4 south  = texture(u_SmokeTex, texCoord - vec2(0.0,        texelSize.y));\n"
            "    vec4 east   = texture(u_SmokeTex, texCoord + vec2(texelSize.x, 0.0));\n"
            "    vec4 west   = texture(u_SmokeTex, texCoord - vec2(texelSize.x, 0.0));\n"
            "    \n"
            "    // Laplacian high-pass: 4*center - (N+S+E+W)\n"
            "    // Computed over the full vec4 but we only USE the .rgb part.\n"
            "    vec4 laplacian = 4.0 * center - (north + south + east + west);\n"
            "    \n"
            "    // FIX 1: sharpen RGB only — do NOT touch the alpha/transmittance.\n"
            "    // Applying the Laplacian to alpha produces ringing at the\n"
            "    // smoke boundary and corrupts the Beer-Lambert blend mask.\n"
            "    vec3 sharpenedRGB = clamp(\n"
            "        center.rgb + laplacian.rgb * u_SharpenStrength,\n"
            "        0.0, 1.0);\n"
            "    \n"
            "    // FIX 2: use the ORIGINAL (unmodified) transmittance for blending.\n"
            "    // center.a is the Beer-Lambert transmittance written by the ray\n"
            "    // marcher: 1.0 = fully transparent, 0.0 = fully opaque smoke.\n"
            "    float transmittance = center.a;\n"
            "    \n"
            "    // Standard over-operator:\n"
            "    //   result = background * transmittance + foreground * (1 - transmittance)\n"
            "    vec3 finalColor = scene.rgb * transmittance\n"
            "                    + sharpenedRGB * (1.0 - transmittance);\n"
            "    \n"
            "    fragColor = vec4(finalColor, 1.0);\n"
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