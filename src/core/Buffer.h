#ifndef BUFFER_H
#define BUFFER_H

#include <glad/glad.h>
#include <iostream>
#include <vector>
#include <cstring>

class SSBOBuffer {
public:
    unsigned int ID = 0;
    size_t size = 0;

    void allocate(size_t bytes) {
        if (ID == 0) glGenBuffers(1, &ID);
        size = bytes;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ID);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void bindBase(GLuint bindingPoint) const {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, ID);
    }

    template<typename T>
    void upload(const std::vector<T>& data) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ID);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, data.size() * sizeof(T), data.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    template<typename T>
    std::vector<T> download(size_t count) const {
        std::vector<T> result(count);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ID);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, count * sizeof(T), result.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return result;
    }

    // Zero-fill using glClearBufferData (GL 4.3)
    void clear() const {
    #ifdef __APPLE__
        std::vector<int> zeros(size / sizeof(int), 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ID);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, zeros.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    #else
        GLint zero = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ID);
        glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    #endif
    }

    void destroy() {
        if (ID != 0) {
            glDeleteBuffers(1, &ID);
            ID = 0;
            size = 0;
        }
    }
};

#endif // BUFFER_H
