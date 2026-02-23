#ifndef NOISE_DEBUG_VIEW_H
#define NOISE_DEBUG_VIEW_H

#include "shader.h"
#include "Texture3D.h"
#include "FullscreenQuad.h"

// Draws a 2D slice of the 3D Worley noise volume as a fullscreen quad.
// Toggle with enabled, adjust sliceZ with Up/Down arrows.
struct NoiseDebugView {
    bool  enabled = false;
    float sliceZ  = 0.5f;   // 0..1 depth slice to display

    void init() {
        const char* vs =
            "#version 430\n"
            "layout(location=0) in vec2 aPos;\n"
            "layout(location=1) in vec2 aUV;\n"
            "out vec2 vUV;\n"
            "void main() { gl_Position = vec4(aPos,0,1); vUV = aUV; }\n";

        const char* fs =
            "#version 430\n"
            "in vec2 vUV;\n"
            "out vec4 FragColor;\n"
            "uniform sampler3D u_NoiseTex;\n"
            "uniform float u_SliceZ;\n"
            "void main() {\n"
            "    float n = texture(u_NoiseTex, vec3(vUV, u_SliceZ)).r;\n"
            "    FragColor = vec4(n,n,n,1);\n"
            "}\n";

        visShader.setUpShader(vs, fs);
    }

    // Call inside the render loop when enabled.
    void draw(Texture3D& noiseTex, FullscreenQuad& quad) {
        glDisable(GL_DEPTH_TEST);
        visShader.use();
        visShader.setInt("u_NoiseTex", 0);
        visShader.setFloat("u_SliceZ", sliceZ);
        noiseTex.bindSampler(0);
        quad.draw();
        glEnable(GL_DEPTH_TEST);
    }

    void destroy() {
        glDeleteProgram(visShader.ID);
    }

private:
    shader visShader;
};

#endif // NOISE_DEBUG_VIEW_H
