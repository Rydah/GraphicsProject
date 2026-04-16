#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include "core/Texture2D.h"
#include "core/Framebuffer.h"
#include "core/shader.h"
#include "core/FullscreenQuad.h"
#include "glVersion.h"

// Bilateral (depth-aware) upsampler.
//
// Replaces standard bilinear/bicubic with a 2x2 bilateral filter: each low-res
// neighbor is weighted by exp(-|depth_center - depth_neighbor| * sigma) so that
// samples across a depth discontinuity (e.g. smoke behind a wall) are rejected.
// This prevents smoke color/transmittance from bleeding across wall edges at
// half-res or quarter-res.
//
// Quarter-res chains two bilateral passes (Q->half, half->full) to limit the
// upscale ratio per pass, matching Acerola's approach.
class Upsampler {
public:
    Texture2D fullResOutput;

    void init(int fullWidth, int fullHeight) {
        fullW = fullWidth;
        fullH = fullHeight;

        fullResOutput.create(fullW,     fullH,     GL_RGBA16F);
        halfTex      .create(fullW / 2, fullH / 2, GL_RGBA16F);

        buildBilateralShader();

        outputFBO.create(); outputFBO.attachColor(fullResOutput.ID);
        halfFBO  .create(); halfFBO  .attachColor(halfTex.ID);

        if (!outputFBO.isComplete()) std::cerr << "Upsampler output FBO incomplete!\n";
        if (!halfFBO  .isComplete()) std::cerr << "Upsampler half FBO incomplete!\n";
    }

    void resize(int fullWidth, int fullHeight) {
        if (fullWidth == fullW && fullHeight == fullH) return;
        fullW = fullWidth;
        fullH = fullHeight;

        fullResOutput.destroy(); outputFBO.destroy();
        halfTex      .destroy(); halfFBO  .destroy();

        fullResOutput.create(fullW,     fullH,     GL_RGBA16F);
        halfTex      .create(fullW / 2, fullH / 2, GL_RGBA16F);

        outputFBO.create(); outputFBO.attachColor(fullResOutput.ID);
        halfFBO  .create(); halfFBO  .attachColor(halfTex.ID);
    }

    // resMode: 1 = half-res input, 2 = quarter-res input.
    // depthTex: full-res scene depth (GL_DEPTH_COMPONENT or R-format depth copy).
    // zNear/zFar: camera clip planes, used to linearise depth for bilateral weighting.
    void upsample(const Texture2D& lowResSmoke,
                  const Texture2D& depthTex,
                  FullscreenQuad&  quad,
                  int   resMode,
                  float zNear,
                  float zFar)
    {
        glDisable(GL_DEPTH_TEST);
        bilateralShader.use();
        bilateralShader.setInt  ("u_Tex",      0);
        bilateralShader.setInt  ("u_DepthTex", 1);
        bilateralShader.setFloat("u_Near",     zNear);
        bilateralShader.setFloat("u_Far",      zFar);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthTex.ID);
        glActiveTexture(GL_TEXTURE0);

        if (resMode == 2) {
            // Pass 1: quarter -> half
            bilateralBlit(lowResSmoke, halfFBO,   fullW / 2, fullH / 2, quad);
            // Pass 2: half -> full
            bilateralBlit(halfTex,     outputFBO, fullW,     fullH,     quad);
        } else {
            // Half -> full, single pass
            bilateralBlit(lowResSmoke, outputFBO, fullW, fullH, quad);
        }

        glEnable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroy() {
        fullResOutput.destroy();
        halfTex      .destroy();
        outputFBO    .destroy();
        halfFBO      .destroy();
        if (bilateralShader.ID) { glDeleteProgram(bilateralShader.ID); bilateralShader.ID = 0; }
    }

private:
    shader      bilateralShader;
    Framebuffer outputFBO;
    Framebuffer halfFBO;
    Texture2D   halfTex;   // RGBA16F ping-pong for quarter-res pass 1
    int fullW = 0, fullH = 0;

    void bilateralBlit(const Texture2D& src, Framebuffer& dstFBO, int dstW, int dstH, FullscreenQuad& quad) {
        dstFBO.bind();
        glViewport(0, 0, dstW, dstH);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform2i(glGetUniformLocation(bilateralShader.ID, "u_TexSize"), src.width, src.height);
        glBindTexture(GL_TEXTURE_2D, src.ID);
        quad.draw();
    }

    void buildBilateralShader() {
        const char* vs = GLSL_VERSION
            "layout(location=0) in vec2 aPos;\n"
            "out vec2 texCoord;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "    texCoord = aPos * 0.5 + 0.5;\n"
            "}\n";

        // Bilateral 2x2 bilinear upsample.
        //
        // For each full-res output pixel:
        //   1. Read scene depth and linearise it.
        //   2. Iterate the 2x2 low-res neighbourhood.
        //   3. For each neighbour, read the scene depth at that position.
        //   4. Weight = bilinear_weight * exp(-|depth_diff| * sigma).
        //      Neighbours across a depth discontinuity (wall edge) get near-zero weight.
        //   5. Normalise. Fallback to nearest-neighbour if all weights collapse.
        //
        // Result: smoke colour + transmittance alpha are depth-correct at wall edges
        // — no bleeding of smoke across geometry boundaries.
        const char* fs = GLSL_VERSION
            "in vec2 texCoord;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D u_Tex;\n"
            "uniform sampler2D u_DepthTex;\n"
            "uniform ivec2     u_TexSize;\n"
            "uniform float     u_Near;\n"
            "uniform float     u_Far;\n"
            "\n"
            "float linearize(float d) {\n"
            "    float z = d * 2.0 - 1.0;\n"
            "    return (2.0 * u_Near * u_Far) / (u_Far + u_Near - z * (u_Far - u_Near));\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    float centerDepth = linearize(texture(u_DepthTex, texCoord).r);\n"
            "\n"
            "    vec2  texSize  = vec2(u_TexSize);\n"
            "    vec2  pixelPos = texCoord * texSize - 0.5;\n"
            "    ivec2 base     = ivec2(floor(pixelPos));\n"
            "    vec2  f        = fract(pixelPos);\n"
            "\n"
            "    vec4  result   = vec4(0.0);\n"
            "    float totalW   = 0.0;\n"
            "\n"
            "    for (int dy = 0; dy <= 1; dy++) {\n"
            "        for (int dx = 0; dx <= 1; dx++) {\n"
            "            ivec2 texel = clamp(base + ivec2(dx, dy), ivec2(0), u_TexSize - 1);\n"
            "            vec2  uv    = (vec2(texel) + 0.5) / texSize;\n"
            "\n"
            "            float nDepth = linearize(texture(u_DepthTex, uv).r);\n"
            "            float depthW = exp(-abs(centerDepth - nDepth) * 100.0);\n"
            "\n"
            "            float bx = (dx == 0) ? (1.0 - f.x) : f.x;\n"
            "            float by = (dy == 0) ? (1.0 - f.y) : f.y;\n"
            "            float w  = bx * by * depthW;\n"
            "\n"
            "            result += texture(u_Tex, uv) * w;\n"
            "            totalW += w;\n"
            "        }\n"
            "    }\n"
            "\n"
            "    if (totalW < 1e-5) {\n"
            "        // All neighbours on the other side of a depth edge — use nearest.\n"
            "        ivec2 nearest = clamp(ivec2(round(pixelPos)), ivec2(0), u_TexSize - 1);\n"
            "        result = texture(u_Tex, (vec2(nearest) + 0.5) / texSize);\n"
            "    } else {\n"
            "        result /= totalW;\n"
            "    }\n"
            "\n"
            "    result.a = clamp(result.a, 0.0, 1.0);\n"
            "    fragColor = result;\n"
            "}\n";

        GLint ok;
        uint32_t vsID = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vsID, 1, &vs, nullptr);
        glCompileShader(vsID);
        glGetShaderiv(vsID, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetShaderInfoLog(vsID, 1024, nullptr, log);
            std::cerr << "Upsampler VS error:\n" << log << "\n";
        }
        uint32_t fsID = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fsID, 1, &fs, nullptr);
        glCompileShader(fsID);
        glGetShaderiv(fsID, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetShaderInfoLog(fsID, 1024, nullptr, log);
            std::cerr << "Upsampler FS error:\n" << log << "\n";
        }
        bilateralShader.ID = glCreateProgram();
        glAttachShader(bilateralShader.ID, vsID);
        glAttachShader(bilateralShader.ID, fsID);
        glLinkProgram(bilateralShader.ID);
        glGetProgramiv(bilateralShader.ID, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(bilateralShader.ID, 1024, nullptr, log);
            std::cerr << "Upsampler link error:\n" << log << "\n";
        }
        glDeleteShader(vsID);
        glDeleteShader(fsID);
    }
};
