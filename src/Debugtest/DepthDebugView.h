#ifndef DEPTH_DEBUG_VIEW_H
#define DEPTH_DEBUG_VIEW_H

#include "core/shader.h"
#include "core/Texture2D.h"
#include "core/FullscreenQuad.h"
#include "glVersion.h"

// Draws the depth buffer as a linearized grayscale fullscreen quad.
// Toggle with `enabled`, configure near/far to match the camera.
struct DepthDebugView {
    bool  enabled = false;
    float zNear   = 0.001f;
    float zFar    = 100.0f;

    void init() {
        const char* vs = GLSL_VERSION
            "layout(location=0) in vec2 aPos;\n"
            "layout(location=1) in vec2 aUV;\n"
            "out vec2 vUV;\n"
            "void main() { gl_Position = vec4(aPos,0,1); vUV = aUV; }\n";

        const char* fs = GLSL_VERSION
            "in vec2 vUV;\n"
            "out vec4 FragColor;\n"
            "uniform sampler2D u_DepthTex;\n"
            "uniform float u_Near;\n"
            "uniform float u_Far;\n"
            "\n"
            "float linearizeDepth(float d) {\n"
            "    float z_ndc = d * 2.0 - 1.0;\n"
            "    return (2.0 * u_Near * u_Far) /\n"
            "           (u_Far + u_Near - z_ndc * (u_Far - u_Near));\n"
            "}\n"
            "\n"
            "void main() {\n"
            "    float d   = texture(u_DepthTex, vUV).r;\n"
            "    float lin = linearizeDepth(d) / u_Far;\n"
            "    FragColor = vec4(vec3(lin), 1.0);\n"
            "}\n";

        visShader.setUpShader(vs, fs);
    }

    // Call inside the render loop when enabled.
    void draw(Texture2D& depthTex, FullscreenQuad& quad) {
        glDisable(GL_DEPTH_TEST);
        visShader.use();
        visShader.setInt  ("u_DepthTex", 0);
        visShader.setFloat("u_Near",     zNear);
        visShader.setFloat("u_Far",      zFar);
        depthTex.bindSampler(0);
        quad.draw();
        glEnable(GL_DEPTH_TEST);
    }

    void destroy() {
        glDeleteProgram(visShader.ID);
    }

private:
    shader visShader;
};

#endif // DEPTH_DEBUG_VIEW_H
