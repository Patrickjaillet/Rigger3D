// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include "render/glad_include.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace rigger::render {
class Shader;
}

namespace rigger::render {

struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::uvec4 joints{0u};
    glm::vec4 weights{0.0f};
};

class MeshRenderer {
public:
    static constexpr std::size_t kMaxBones = 256;

    MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;
    MeshRenderer(MeshRenderer&& other) noexcept;
    MeshRenderer& operator=(MeshRenderer&& other) noexcept;
    ~MeshRenderer();

    void setMesh(const std::vector<MeshVertex>& vertices, const std::vector<std::uint32_t>& indices,
                 GLenum primitive = GL_TRIANGLES);
    bool setBoneMatrices(const std::vector<glm::mat4>& matrices);
    // Skinning par quaternions duaux (CPU uniquement) : à utiliser avec draw(shader, false, ...).
    void setDualQuaternionSkinning(bool enabled) { useDqs_ = enabled; }
    bool dualQuaternionSkinning() const { return useDqs_; }
    void draw(const Shader& shader, bool gpuSkinning, std::uint32_t selectedBone);
    void draw() const;

    GLuint vao() const { return vao_; }
    GLenum primitive() const { return primitive_; }

private:
    void ensureStorage();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLuint boneBuffer_ = 0;
    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<glm::mat4> boneMatrices_;
    GLsizei indexCount_ = 0;
    GLenum primitive_ = GL_TRIANGLES;
    bool cpuDataUploaded_ = false;
    bool useDqs_ = false;
};

}  // namespace rigger::render
