#ifndef COMPUTE_SHADER_H
#define COMPUTE_SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>

class ComputeShader {
public:
    unsigned int ID = 0;

    void setUp(const char* source) {
        unsigned int cs = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(cs, 1, &source, NULL);
        glCompileShader(cs);

        int success;
        char infoLog[1024];
        glGetShaderiv(cs, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(cs, 1024, NULL, infoLog);
            std::cout << "ERROR::COMPUTE_SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
        }

        ID = glCreateProgram();
        glAttachShader(ID, cs);
        glLinkProgram(ID);

        glGetProgramiv(ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(ID, 1024, NULL, infoLog);
            std::cout << "ERROR::COMPUTE_SHADER::LINKING_FAILED\n" << infoLog << std::endl;
        }
        glDeleteShader(cs);

        // Cache local work group size
        glGetProgramiv(ID, GL_COMPUTE_WORK_GROUP_SIZE, localSize);
    }

    void use() const {
        glUseProgram(ID);
    }

    // Dispatch with automatic ceil-division to cover totalX * totalY * totalZ work items
    void dispatch(int totalX, int totalY = 1, int totalZ = 1) const {
        glUseProgram(ID);
        glDispatchCompute(
            (totalX + localSize[0] - 1) / localSize[0],
            (totalY + localSize[1] - 1) / localSize[1],
            (totalZ + localSize[2] - 1) / localSize[2]
        );
    }

    // Uniform setters
    void setInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setVec3(const std::string& name, const glm::vec3& v) const {
        glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(v));
    }
    void setIVec3(const std::string& name, const glm::ivec3& v) const {
        glUniform3iv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(v));
    }
    void setMat4(const std::string& name, const glm::mat4& m) const {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, glm::value_ptr(m));
    }

private:
    GLint localSize[3] = {1, 1, 1};
};

#endif // COMPUTE_SHADER_H
