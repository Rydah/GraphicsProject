#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include "core/Texture2D.h"
#include "core/Framebuffer.h"
#include "core/shader.h"
#include "core/FullscreenQuad.h"
#include "glVersion.h"

class Upsampler {
public:
    Texture2D   fullResOutput;
    
    void init(int fullWidth, int fullHeight) {
        fullW = fullWidth;
        fullH = fullHeight;
        
        fullResOutput.create(fullW, fullH, GL_RGBA16F);
        
        // Setup upsampler shader (Catmull-Rom bicubic)
        buildUpsampler();
        buildBlitShader();
        
        // Setup output FBO for upsampling pass
        outputFBO.create();
        outputFBO.attachColor(fullResOutput.ID);
        if (!outputFBO.isComplete()) {
            std::cerr << "Upsampler output FBO incomplete!" << std::endl;
        }
    }
    
    void resize(int fullWidth, int fullHeight) {
        if (fullWidth == fullW && fullHeight == fullH) return;
        fullW = fullWidth;
        fullH = fullHeight;
        
        fullResOutput.destroy();
        fullResOutput.create(fullW, fullH, GL_RGBA16F);
        
        outputFBO.destroy();
        outputFBO.create();
        outputFBO.attachColor(fullResOutput.ID);
        if (!outputFBO.isComplete()) {
            std::cerr << "Upsampler output FBO incomplete after resize!" << std::endl;
        }
    }
    
    // Upsample from half-res texture to full-res
    // Applies Catmull-Rom bicubic filtering
    void upsample(const Texture2D& halfResTex, FullscreenQuad& quad) {
        outputFBO.bind();
        glViewport(0, 0, fullW, fullH);
        glClear(GL_COLOR_BUFFER_BIT);
        
        upsamplerShader.use();
        upsamplerShader.setInt("u_HalfResTex", 0);
        glUniform2i(glGetUniformLocation(upsamplerShader.ID, "u_HalfResSize"), halfResTex.width, halfResTex.height);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, halfResTex.ID);
        
        glDisable(GL_DEPTH_TEST);
        quad.draw();
        glEnable(GL_DEPTH_TEST);
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    // Blit the upsampled result to screen
    void blit(FullscreenQuad& quad) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_SRC_ALPHA);
        
        blitShader.use();
        blitShader.setInt("u_SmokeTex", 0);
        fullResOutput.bindSampler(0);
        
        glDisable(GL_DEPTH_TEST);
        quad.draw();
        glEnable(GL_DEPTH_TEST);
        
        glDisable(GL_BLEND);
    }
    
    void destroy() {
        fullResOutput.destroy();
        outputFBO.destroy();
        if (upsamplerShader.ID) {
            glDeleteProgram(upsamplerShader.ID);
            upsamplerShader.ID = 0;
        }
        if (blitShader.ID) {
            glDeleteProgram(blitShader.ID);
            blitShader.ID = 0;
        }
    }
    
private:
    shader upsamplerShader;
    shader blitShader;
    Framebuffer outputFBO;
    int fullW = 0, fullH = 0;
    
    void buildUpsampler() {
        const char* vs = GLSL_VERSION
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 texCoord;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "    texCoord = aPos * 0.5 + 0.5;\n"
            "}\n";
            
        // FIX 1: catmullRom takes float, not vec2.
        // FIX 2: renamed local variable 'sample' -> 'texSample' ('sample' is a reserved
        //         GLSL keyword for multisampling and causes a compile error on GL 4.30+).
        const char* fs = GLSL_VERSION
            "in vec2 texCoord;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D u_HalfResTex;\n"
            "uniform ivec2 u_HalfResSize;\n"
            "\n"
            "vec4 catmullRom(float f) {\n"
            "    vec4 w;\n"
            "    w.x =  f*(-0.5 + f*(1.0 - 0.5*f));\n"
            "    w.y =  1.0 + f*f*(-2.5 + 1.5*f);\n"
            "    w.z =  f*(0.5 + f*(2.0 - 1.5*f));\n"
            "    w.w =  f*f*(-0.5 + 0.5*f);\n"
            "    return w;\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    // Map full-res texture coordinate to half-res\n"
            "    vec2 halfResCoord = texCoord * vec2(u_HalfResSize);\n"
            "    vec2 baseCoord = floor(halfResCoord - 0.5);\n"
            "    vec2 frac = fract(halfResCoord - 0.5);\n"
            "    \n"
            "    // Clamp base coordinate to valid range\n"
            "    baseCoord = max(vec2(0), min(vec2(u_HalfResSize - 2), baseCoord));\n"
            "    \n"
            "    // Get Catmull-Rom weights for x and y\n"
            "    vec4 wx = catmullRom(frac.x);\n"
            "    vec4 wy = catmullRom(frac.y);\n"
            "    \n"
            "    vec4 color = vec4(0.0);\n"
            "    float totalWeight = 0.0;\n"
            "    \n"
            "    // 4x4 neighborhood\n"
            "    for (int dy = -1; dy <= 2; dy++) {\n"
            "        for (int dx = -1; dx <= 2; dx++) {\n"
            "            vec2 sampleCoord = baseCoord + vec2(dx, dy);\n"
            "            sampleCoord = clamp(sampleCoord, vec2(0), vec2(u_HalfResSize - 1));\n"
            "            \n"
            "            float w = wx[dx + 1] * wy[dy + 1];\n"
            "            vec4 texSample = texture(u_HalfResTex, (sampleCoord + 0.5) / vec2(u_HalfResSize));\n"
            "            color += texSample * w;\n"
            "            totalWeight += w;\n"
            "        }\n"
            "    }\n"
            "    \n"
            "    color = color / max(totalWeight, 0.0001);\n"
            "    // Clamp alpha to [0,1] range for safety\n"
            "    color.a = clamp(color.a, 0.0, 1.0);\n"
            "    \n"
            "    fragColor = color;\n"
            "}\n";
        
        GLint vs_ok, fs_ok, prog_ok;
        uint32_t vs_id = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs_id, 1, &vs, nullptr);
        glCompileShader(vs_id);
        glGetShaderiv(vs_id, GL_COMPILE_STATUS, &vs_ok);
        if (!vs_ok) {
            char log[1024] = {};
            glGetShaderInfoLog(vs_id, 1024, nullptr, log);
            std::cerr << "Upsampler VS compile error:\n" << log << std::endl;
        }
        
        uint32_t fs_id = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs_id, 1, &fs, nullptr);
        glCompileShader(fs_id);
        glGetShaderiv(fs_id, GL_COMPILE_STATUS, &fs_ok);
        if (!fs_ok) {
            char log[1024] = {};
            glGetShaderInfoLog(fs_id, 1024, nullptr, log);
            std::cerr << "Upsampler FS compile error:\n" << log << std::endl;
        }
        
        upsamplerShader.ID = glCreateProgram();
        glAttachShader(upsamplerShader.ID, vs_id);
        glAttachShader(upsamplerShader.ID, fs_id);
        glLinkProgram(upsamplerShader.ID);
        glGetProgramiv(upsamplerShader.ID, GL_LINK_STATUS, &prog_ok);
        if (!prog_ok) {
            char log[1024] = {};
            glGetProgramInfoLog(upsamplerShader.ID, 1024, nullptr, log);
            std::cerr << "Upsampler shader link error:\n" << log << std::endl;
        }
        
        glDeleteShader(vs_id);
        glDeleteShader(fs_id);
    }
    
    void buildBlitShader() {
        const char* vs = GLSL_VERSION
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 texCoord;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "    texCoord = aPos * 0.5 + 0.5;\n"
            "}\n";
        
        const char* fs = GLSL_VERSION
            "in vec2 texCoord;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D u_SmokeTex;\n"
            "void main() {\n"
            "    vec4 smoke = texture(u_SmokeTex, texCoord);\n"
            "    fragColor = smoke;\n"
            "}\n";
        
        GLint vs_ok, fs_ok, prog_ok;
        uint32_t vs_id = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs_id, 1, &vs, nullptr);
        glCompileShader(vs_id);
        glGetShaderiv(vs_id, GL_COMPILE_STATUS, &vs_ok);
        if (!vs_ok) {
            char log[1024] = {};
            glGetShaderInfoLog(vs_id, 1024, nullptr, log);
            std::cerr << "Upsampler blit VS compile error:\n" << log << std::endl;
        }
        
        uint32_t fs_id = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs_id, 1, &fs, nullptr);
        glCompileShader(fs_id);
        glGetShaderiv(fs_id, GL_COMPILE_STATUS, &fs_ok);
        if (!fs_ok) {
            char log[1024] = {};
            glGetShaderInfoLog(fs_id, 1024, nullptr, log);
            std::cerr << "Upsampler blit FS compile error:\n" << log << std::endl;
        }
        
        blitShader.ID = glCreateProgram();
        glAttachShader(blitShader.ID, vs_id);
        glAttachShader(blitShader.ID, fs_id);
        glLinkProgram(blitShader.ID);
        glGetProgramiv(blitShader.ID, GL_LINK_STATUS, &prog_ok);
        if (!prog_ok) {
            char log[1024] = {};
            glGetProgramInfoLog(blitShader.ID, 1024, nullptr, log);
            std::cerr << "Upsampler blit shader link error:\n" << log << std::endl;
        }
        
        glDeleteShader(vs_id);
        glDeleteShader(fs_id);
    }
};