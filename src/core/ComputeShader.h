#ifndef COMPUTE_SHADER_H
#define COMPUTE_SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include "core/FileUtils.h"

class ComputeShader {
public:
    unsigned int ID = 0;
    bool valid = false;   // true only after successful compile+link

    void setUp(const char* source) {
        valid = false;

        unsigned int cs = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(cs, 1, &source, NULL);
        glCompileShader(cs);

        int success;
        char infoLog[1024];
        glGetShaderiv(cs, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(cs, 1024, NULL, infoLog);
            std::cout << "ERROR::COMPUTE_SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
            glDeleteShader(cs);
            return;
        }

        ID = glCreateProgram();
        glAttachShader(ID, cs);
        glLinkProgram(ID);

        glGetProgramiv(ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(ID, 1024, NULL, infoLog);
            std::cout << "ERROR::COMPUTE_SHADER::LINKING_FAILED\n" << infoLog << std::endl;
            glDeleteShader(cs);
            return;
        }
        glDeleteShader(cs);

        // Cache local work group size
        glGetProgramiv(ID, GL_COMPUTE_WORK_GROUP_SIZE, localSize);
        valid = true;

        std::cout << "  -> local_size = ("
                  << localSize[0] << ", " << localSize[1] << ", " << localSize[2] << ")\n";
    }

    void setUpFromFile(const std::string& path) {
        std::string src;
        try {
            src = loadTextFile(path);
        } catch (const std::exception& e) {
            std::cout << "ERROR::COMPUTE_SHADER::FILE_NOT_FOUND: " << path
                      << "\n  (working directory matters — run from project root)\n";
            return;
        }
        std::cout << "[ComputeShader] Compiling " << path << std::endl;
        setUp(src.c_str());
    }

    void use() const {
        if (!valid) return;
        glUseProgram(ID);
    }

    // Dispatch with automatic ceil-division to cover totalX * totalY * totalZ work items
    void dispatch(int totalX, int totalY = 1, int totalZ = 1) const {
        if (!valid) {
            std::cout << "WARNING: Skipping dispatch on invalid compute shader (ID="
                      << ID << ")\n";
            return;
        }
        glUseProgram(ID);
        glDispatchCompute(
            (totalX + localSize[0] - 1) / localSize[0],
            (totalY + localSize[1] - 1) / localSize[1],
            (totalZ + localSize[2] - 1) / localSize[2]
        );
    }

    // Uniform setters (guarded — no-op if shader is invalid)
    void setInt(const std::string& name, int value) const {
        if (!valid) return;
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setFloat(const std::string& name, float value) const {
        if (!valid) return;
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setVec3(const std::string& name, const glm::vec3& v) const {
        if (!valid) return;
        glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(v));
    }
    void setIVec3(const std::string& name, const glm::ivec3& v) const {
        if (!valid) return;
        glUniform3iv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(v));
    }
    void setMat4(const std::string& name, const glm::mat4& m) const {
        if (!valid) return;
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, glm::value_ptr(m));
    }

private:
    GLint localSize[3] = {1, 1, 1};
};

#endif // COMPUTE_SHADER_H
