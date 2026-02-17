#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <glad/glad.h>
#include <iostream>

class Framebuffer {
public:
    unsigned int ID = 0;

    void create() {
        glGenFramebuffers(1, &ID);
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, ID);
    }

    static void unbind() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Attach a 2D color texture at the given attachment index
    void attachColor(unsigned int texID, int index = 0) const {
        glBindFramebuffer(GL_FRAMEBUFFER, ID);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, texID, 0);
    }

    // Attach a depth texture
    void attachDepth(unsigned int texID) const {
        glBindFramebuffer(GL_FRAMEBUFFER, ID);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texID, 0);
    }

    // Set up a depth-only FBO (no color writes)
    void setDepthOnly() const {
        glBindFramebuffer(GL_FRAMEBUFFER, ID);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    bool isComplete() const {
        glBindFramebuffer(GL_FRAMEBUFFER, ID);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cout << "ERROR::FRAMEBUFFER not complete: 0x" << std::hex << status << std::dec << std::endl;
            return false;
        }
        return true;
    }

    void destroy() {
        if (ID != 0) {
            glDeleteFramebuffers(1, &ID);
            ID = 0;
        }
    }
};

#endif // FRAMEBUFFER_H
