#ifndef TEXTURE2D_H
#define TEXTURE2D_H

#include <glad/glad.h>
#include <iostream>

class Texture2D {
public:
    unsigned int ID = 0;
    int width = 0, height = 0;
    GLenum internalFormat = 0;

    // Create immutable 2D texture with glTexStorage2D
    void create(int w, int h, GLenum format) {
        width = w; height = h;
        internalFormat = format;

        glGenTextures(1, &ID);
        glBindTexture(GL_TEXTURE_2D, ID);
        glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Bind as image for compute shader read/write
    void bindImage(GLuint unit, GLenum access) const {
        glBindImageTexture(unit, ID, 0, GL_FALSE, 0, access, internalFormat);
    }

    // Bind as sampler for texture() lookups
    void bindSampler(GLuint unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, ID);
    }

    void destroy() {
        if (ID != 0) {
            glDeleteTextures(1, &ID);
            ID = 0;
        }
    }
};

#endif // TEXTURE2D_H
