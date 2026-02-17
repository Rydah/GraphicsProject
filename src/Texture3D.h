#ifndef TEXTURE3D_H
#define TEXTURE3D_H

#include <glad/glad.h>
#include <iostream>

class Texture3D {
public:
    unsigned int ID = 0;
    int width = 0, height = 0, depth = 0;
    GLenum internalFormat = 0;

    // Create immutable 3D texture with glTexStorage3D (required for imageStore/imageLoad)
    void create(int w, int h, int d, GLenum format) {
        width = w; height = h; depth = d;
        internalFormat = format;

        glGenTextures(1, &ID);
        glBindTexture(GL_TEXTURE_3D, ID);
        glTexStorage3D(GL_TEXTURE_3D, 1, format, w, h, d);

        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);
    }

    // Bind as image for compute shader read/write
    // access: GL_READ_ONLY, GL_WRITE_ONLY, or GL_READ_WRITE
    void bindImage(GLuint unit, GLenum access) const {
        glBindImageTexture(unit, ID, 0, GL_TRUE, 0, access, internalFormat);
    }

    // Bind as sampler for texture() lookups in fragment/compute shaders
    void bindSampler(GLuint unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_3D, ID);
    }

    void destroy() {
        if (ID != 0) {
            glDeleteTextures(1, &ID);
            ID = 0;
        }
    }
};

#endif // TEXTURE3D_H
