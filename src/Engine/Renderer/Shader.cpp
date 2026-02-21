#include "Engine/Renderer/Shader.h"
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif

namespace Engine {
    Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& computePath) {
        std::string vertexCode;
        std::string fragmentCode;
        std::string computeCode;

        if (!vertexPath.empty()) vertexCode = ReadFile(vertexPath);
        if (!fragmentPath.empty()) fragmentCode = ReadFile(fragmentPath);
        if (!computePath.empty()) computeCode = ReadFile(computePath);

        unsigned int vertex, fragment, compute;
        
        m_ID = glCreateProgram();

        if (!vertexCode.empty()) {
            vertex = CompileShader(GL_VERTEX_SHADER, vertexCode);
            glAttachShader(m_ID, vertex);
        }
        
        if (!fragmentCode.empty()) {
            fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentCode);
            glAttachShader(m_ID, fragment);
        }

        if (!computeCode.empty()) {
            compute = CompileShader(GL_COMPUTE_SHADER, computeCode);
            glAttachShader(m_ID, compute);
        }

        glLinkProgram(m_ID);

        // Check linking errors
        int success;
        char infoLog[1024];
        glGetProgramiv(m_ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(m_ID, 1024, NULL, infoLog);
            std::cerr << "SHADER_LINKING_ERROR\n" << infoLog << std::endl;
        }

        if (!vertexCode.empty()) glDeleteShader(vertex);
        if (!fragmentCode.empty()) glDeleteShader(fragment);
        if (!computeCode.empty()) glDeleteShader(compute);
    }

    Shader::~Shader() {
        glDeleteProgram(m_ID);
    }

    void Shader::Use() const {
        glUseProgram(m_ID);
    }

    std::string Shader::ReadFile(const std::string& path) {
        std::ifstream file;
        std::stringstream buf;
        
        file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try {
            file.open(path);
            buf << file.rdbuf();
            file.close();
        } catch (std::ifstream::failure& e) {
            std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ: " << path << std::endl;
        }
        return buf.str();
    }

    unsigned int Shader::CompileShader(unsigned int type, const std::string& source) {
        unsigned int id = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(id, 1, &src, NULL);
        glCompileShader(id);

        int success;
        char infoLog[1024];
        glGetShaderiv(id, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(id, 1024, NULL, infoLog);
            std::cerr << "SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << std::endl;
        }
        return id;
    }

    void Shader::SetBool(const std::string& name, bool value) const {
        glUniform1i(glGetUniformLocation(m_ID, name.c_str()), (int)value);
    }
    void Shader::SetInt(const std::string& name, int value) const {
        glUniform1i(glGetUniformLocation(m_ID, name.c_str()), value);
    }
    void Shader::SetFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(m_ID, name.c_str()), value);
    }
    void Shader::SetVec2(const std::string& name, const glm::vec2& value) const {
        glUniform2fv(glGetUniformLocation(m_ID, name.c_str()), 1, &value[0]);
    }
    void Shader::SetVec3(const std::string& name, const glm::vec3& value) const {
        glUniform3fv(glGetUniformLocation(m_ID, name.c_str()), 1, &value[0]);
    }
    void Shader::SetVec4(const std::string& name, const glm::vec4& value) const {
        glUniform4fv(glGetUniformLocation(m_ID, name.c_str()), 1, &value[0]);
    }
    void Shader::SetMat4(const std::string& name, const glm::mat4& mat) const {
        glUniformMatrix4fv(glGetUniformLocation(m_ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }
}
