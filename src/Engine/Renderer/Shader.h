#pragma once

#include <string>
#include <glm/glm.hpp>

namespace Engine {
    class Shader {
    public:
        Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& computePath = "");
        ~Shader();

        void Use() const;

        void SetBool(const std::string& name, bool value) const;
        void SetInt(const std::string& name, int value) const;
        void SetFloat(const std::string& name, float value) const;
        void SetVec2(const std::string& name, const glm::vec2& value) const;
        void SetVec3(const std::string& name, const glm::vec3& value) const;
        void SetVec4(const std::string& name, const glm::vec4& value) const;
        void SetMat4(const std::string& name, const glm::mat4& mat) const;

        unsigned int GetID() const { return m_ID; }

    private:
        unsigned int m_ID;
        
        std::string ReadFile(const std::string& path);
        unsigned int CompileShader(unsigned int type, const std::string& source);
    };
}
