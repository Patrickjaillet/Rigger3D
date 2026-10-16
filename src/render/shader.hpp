// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <string>

namespace rigger::render {

class Shader {
public:
    Shader() = default;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    ~Shader();

    bool compileFromSource(const std::string& vertexSource, const std::string& fragmentSource);
    bool compileFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
    void bind() const;
    void unbind() const;
    bool isValid() const { return program_ != 0; }
    GLuint id() const { return program_; }

    void setUniform(const char* name, const glm::mat4& value) const;
    void setUniform(const char* name, const glm::vec3& value) const;
    void setUniform(const char* name, const glm::vec4& value) const;
    void setUniform(const char* name, float value) const;
    void setUniform(const char* name, int value) const;

private:
    static bool compileStage(GLuint shader, const std::string& source, const char* typeName,
                             std::string* infoLog);
    static std::string readFile(const std::string& path);

    GLuint program_ = 0;
};

}  // namespace rigger::render
