#ifndef SHADER_H
#define SHADER_H

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>

class shader {

public:
    unsigned int ID = 0;

    void use() {
        glUseProgram(ID);
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
    void setVec4(const std::string& name, const glm::vec4& v) const {
        glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(v));
    }
    void setMat4(const std::string& name, const glm::mat4& m) const {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, glm::value_ptr(m));
    }
    void setIVec3(const std::string& name, const glm::ivec3& v) const {
        glUniform3iv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(v));
    }

    void setUpShader(const char* vertexShaderSource, const char* fragmentShaderSource) {
        unsigned int vs = compileStage(GL_VERTEX_SHADER, vertexShaderSource, "VERTEX");
        unsigned int fs = compileStage(GL_FRAGMENT_SHADER, fragmentShaderSource, "FRAGMENT");

        ID = glCreateProgram();
        glAttachShader(ID, vs);
        glAttachShader(ID, fs);
        linkProgram();
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void setUpShader(const char* vertexShaderSource, const char* fragmentShaderSource, const char* geometryShaderSource) {
        unsigned int vs = compileStage(GL_VERTEX_SHADER, vertexShaderSource, "VERTEX");
        unsigned int fs = compileStage(GL_FRAGMENT_SHADER, fragmentShaderSource, "FRAGMENT");
        unsigned int gs = compileStage(GL_GEOMETRY_SHADER, geometryShaderSource, "GEOMETRY");

        ID = glCreateProgram();
        glAttachShader(ID, vs);
        glAttachShader(ID, fs);
        glAttachShader(ID, gs);
        linkProgram();
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteShader(gs);
    }

private:
    unsigned int compileStage(GLenum type, const char* source, const char* label) {
        unsigned int s = glCreateShader(type);
        glShaderSource(s, 1, &source, NULL);
        glCompileShader(s);
        int success;
        char infoLog[1024];
        glGetShaderiv(s, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(s, 1024, NULL, infoLog);
            std::cout << "ERROR::SHADER::" << label << "::COMPILATION_FAILED\n" << infoLog << std::endl;
        }
        return s;
    }

    void linkProgram() {
        glLinkProgram(ID);
        int success;
        char infoLog[1024];
        glGetProgramiv(ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(ID, 1024, NULL, infoLog);
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        }
    }
};

#endif // SHADER_H
