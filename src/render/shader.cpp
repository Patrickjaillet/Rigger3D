// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/shader.hpp"

#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <sstream>
#include <utility>

namespace rigger::render {

namespace {

std::string shaderTypeName(GLenum type) {
    switch (type) {
        case GL_VERTEX_SHADER:
            return "vertex";
        case GL_FRAGMENT_SHADER:
            return "fragment";
        default:
            return "unknown";
    }
}

}  // namespace

Shader::Shader(Shader&& other) noexcept : program_(std::exchange(other.program_, 0)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
        program_ = std::exchange(other.program_, 0);
    }
    return *this;
}

Shader::~Shader() {
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
}

std::string Shader::readFile(const std::string& path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool Shader::compileStage(GLuint shader, const std::string& source, const char* typeName,
                          std::string* infoLog) {
    const GLchar* sourcePtr = source.c_str();
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        if (length > 0) {
            std::string log(static_cast<size_t>(length), '\0');
            glGetShaderInfoLog(shader, length, nullptr, log.data());
            if (infoLog != nullptr) {
                *infoLog = log;
            }
        }
        return false;
    }

    if (infoLog != nullptr) {
        infoLog->clear();
    }
    (void)typeName;
    return true;
}

bool Shader::compileFromSource(const std::string& vertexSource, const std::string& fragmentSource) {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    std::string log;

    if (!compileStage(vertexShader, vertexSource, shaderTypeName(GL_VERTEX_SHADER).c_str(), &log) ||
        !compileStage(fragmentShader, fragmentSource, shaderTypeName(GL_FRAGMENT_SHADER).c_str(), &log)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    GLuint newProgram = glCreateProgram();
    glAttachShader(newProgram, vertexShader);
    glAttachShader(newProgram, fragmentShader);
    glLinkProgram(newProgram);

    GLint linked = 0;
    glGetProgramiv(newProgram, GL_LINK_STATUS, &linked);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (linked == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(newProgram, GL_INFO_LOG_LENGTH, &length);
        if (length > 0) {
            std::string logText(static_cast<size_t>(length), '\0');
            glGetProgramInfoLog(newProgram, length, nullptr, logText.data());
            (void)logText;
        }
        glDeleteProgram(newProgram);
        return false;
    }

    program_ = newProgram;
    return true;
}

bool Shader::compileFromFiles(const std::string& vertexPath, const std::string& fragmentPath) {
    const std::string vertexSource = readFile(vertexPath);
    const std::string fragmentSource = readFile(fragmentPath);
    if (vertexSource.empty() || fragmentSource.empty()) {
        return false;
    }
    return compileFromSource(vertexSource, fragmentSource);
}

void Shader::bind() const {
    if (program_ != 0) {
        glUseProgram(program_);
    }
}

void Shader::unbind() const { glUseProgram(0); }

void Shader::setUniform(const char* name, const glm::mat4& value) const {
    if (program_ != 0) {
        const GLint location = glGetUniformLocation(program_, name);
        if (location >= 0) {
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
        }
    }
}

void Shader::setUniform(const char* name, const glm::vec3& value) const {
    if (program_ != 0) {
        const GLint location = glGetUniformLocation(program_, name);
        if (location >= 0) {
            glUniform3fv(location, 1, glm::value_ptr(value));
        }
    }
}

void Shader::setUniform(const char* name, const glm::vec4& value) const {
    if (program_ != 0) {
        const GLint location = glGetUniformLocation(program_, name);
        if (location >= 0) {
            glUniform4fv(location, 1, glm::value_ptr(value));
        }
    }
}

void Shader::setUniform(const char* name, float value) const {
    if (program_ != 0) {
        const GLint location = glGetUniformLocation(program_, name);
        if (location >= 0) {
            glUniform1f(location, value);
        }
    }
}

void Shader::setUniform(const char* name, int value) const {
    if (program_ != 0) {
        const GLint location = glGetUniformLocation(program_, name);
        if (location >= 0) {
            glUniform1i(location, value);
        }
    }
}

}  // namespace rigger::render
