// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/gpu_skinning.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "core/skinning/lbs.hpp"
#include "render/skinning_shader.hpp"

namespace rigger::render {
namespace {

constexpr std::string_view kVertexPrefix = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
out vec3 tfPosition;
out vec3 tfNormal;
)";

constexpr std::string_view kVertexSuffix = R"(
void main() {
    tfPosition = skinPosition(aPosition, aJoints, aWeights);
    tfNormal = skinNormal(aNormal, aJoints, aWeights);
    gl_Position = vec4(tfPosition, 1.0);
}
)";

std::string shaderLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::string message(static_cast<std::size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, message.data());
    return message;
}

std::string programLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::string message(static_cast<std::size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, message.data());
    return message;
}

}  // namespace

Result<GpuSkinningResult> skinVerticesGpu(const Mesh& mesh, const std::vector<glm::mat4>& skinMatrices) {
    const Result<std::vector<Vertex>> cpuValidation = skinLbs(mesh, skinMatrices);
    if (!cpuValidation) {
        return cpuValidation.error();
    }
    if (mesh.vertices.empty()) {
        return GpuSkinningResult{};
    }
    if (mesh.skinWeights.empty()) {
        return Error{"GPU skinning requires skin weights for every vertex"};
    }
    if (skinMatrices.size() > MeshRenderer::kMaxBones) {
        return Error{"GPU skinning supports at most 256 bone matrices"};
    }

    GLint maxUniformBlockSize = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUniformBlockSize);
    if (maxUniformBlockSize < static_cast<GLint>(MeshRenderer::kMaxBones * sizeof(glm::mat4))) {
        return Error{"OpenGL uniform block limit is smaller than the 256-bone palette"};
    }

    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    GLint previousUniformBuffer = 0;
    GLint previousUniformBufferBase = 0;
    GLint previousTransformFeedback = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previousUniformBuffer);
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &previousUniformBufferBase);
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BINDING, &previousTransformFeedback);
    const GLboolean rasterizerDiscardEnabled = glIsEnabled(GL_RASTERIZER_DISCARD);

    GLuint shader = glCreateShader(GL_VERTEX_SHADER);
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
    GLuint paletteBuffer = 0;
    GLuint feedbackBuffer = 0;
    GLuint transformFeedback = 0;
    GLuint timerQuery = 0;
    auto cleanup = [&]() {
        glUseProgram(static_cast<GLuint>(previousProgram));
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, static_cast<GLuint>(previousUniformBufferBase));
        glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(previousUniformBuffer));
        glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, static_cast<GLuint>(previousTransformFeedback));
        if (rasterizerDiscardEnabled == GL_FALSE) {
            glDisable(GL_RASTERIZER_DISCARD);
        } else {
            glEnable(GL_RASTERIZER_DISCARD);
        }
        if (timerQuery != 0) glDeleteQueries(1, &timerQuery);
        if (transformFeedback != 0) glDeleteTransformFeedbacks(1, &transformFeedback);
        if (feedbackBuffer != 0) glDeleteBuffers(1, &feedbackBuffer);
        if (paletteBuffer != 0) glDeleteBuffers(1, &paletteBuffer);
        if (vertexBuffer != 0) glDeleteBuffers(1, &vertexBuffer);
        if (vertexArray != 0) glDeleteVertexArrays(1, &vertexArray);
        if (program != 0) glDeleteProgram(program);
        if (shader != 0) glDeleteShader(shader);
    };

    const std::string source =
        std::string(kVertexPrefix) + std::string(kSkinningGlsl) + std::string(kVertexSuffix);
    const char* sourcePointer = source.c_str();
    glShaderSource(shader, 1, &sourcePointer, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        const std::string log = shaderLog(shader);
        cleanup();
        return Error{"GPU skinning vertex shader compilation failed: " + log};
    }

    program = glCreateProgram();
    glAttachShader(program, shader);
    const std::array<const GLchar*, 2> feedbackVaryings{"tfPosition", "tfNormal"};
    glTransformFeedbackVaryings(program, static_cast<GLsizei>(feedbackVaryings.size()),
                                feedbackVaryings.data(), GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        const std::string log = programLog(program);
        cleanup();
        return Error{"GPU skinning program link failed: " + log};
    }

    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    std::vector<MeshVertex> packedVertices(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        packedVertices[i].position = mesh.vertices[i].position;
        packedVertices[i].normal = mesh.vertices[i].normal;
        packedVertices[i].color = glm::vec3(1.0f);
        packedVertices[i].joints = {mesh.skinWeights[i].joints[0], mesh.skinWeights[i].joints[1],
                                    mesh.skinWeights[i].joints[2], mesh.skinWeights[i].joints[3]};
        packedVertices[i].weights = {mesh.skinWeights[i].weights[0], mesh.skinWeights[i].weights[1],
                                     mesh.skinWeights[i].weights[2], mesh.skinWeights[i].weights[3]};
    }
    glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(MeshVertex)),
                 packedVertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_INT, sizeof(MeshVertex),
                           reinterpret_cast<void*>(offsetof(MeshVertex, joints)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          reinterpret_cast<void*>(offsetof(MeshVertex, weights)));

    std::array<glm::mat4, MeshRenderer::kMaxBones> palette;
    palette.fill(glm::mat4(1.0f));
    std::copy(skinMatrices.begin(), skinMatrices.end(), palette.begin());
    glGenBuffers(1, &paletteBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, paletteBuffer);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(palette), palette.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, paletteBuffer);
    const GLuint blockIndex = glGetUniformBlockIndex(program, "SkinMatrices");
    glUniformBlockBinding(program, blockIndex, 0);

    glGenBuffers(1, &feedbackBuffer);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, feedbackBuffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float) * 6), nullptr, GL_STREAM_READ);
    glGenTransformFeedbacks(1, &transformFeedback);
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, transformFeedback);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, feedbackBuffer);
    glGenQueries(1, &timerQuery);

    glUseProgram(program);
    glEnable(GL_RASTERIZER_DISCARD);
    glBeginQuery(GL_TIME_ELAPSED, timerQuery);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mesh.vertices.size()));
    glEndTransformFeedback();
    glEndQuery(GL_TIME_ELAPSED);
    glDisable(GL_RASTERIZER_DISCARD);

    GLuint64 elapsed = 0;
    glGetQueryObjectui64v(timerQuery, GL_QUERY_RESULT, &elapsed);
    std::vector<std::array<float, 6>> output(mesh.vertices.size());
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                       static_cast<GLsizeiptr>(output.size() * sizeof(output.front())), output.data());

    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        cleanup();
        return Error{"OpenGL error during GPU skinning: " + std::to_string(glError)};
    }

    GpuSkinningResult result;
    result.gpuTimeNanoseconds = static_cast<std::uint64_t>(elapsed);
    result.vertices.resize(mesh.vertices.size());
    for (std::size_t i = 0; i < output.size(); ++i) {
        result.vertices[i].position = {output[i][0], output[i][1], output[i][2]};
        result.vertices[i].normal = {output[i][3], output[i][4], output[i][5]};
        result.vertices[i].color = glm::vec3(1.0f);
        result.vertices[i].joints = {mesh.skinWeights[i].joints[0], mesh.skinWeights[i].joints[1],
                                     mesh.skinWeights[i].joints[2], mesh.skinWeights[i].joints[3]};
        result.vertices[i].weights = {mesh.skinWeights[i].weights[0], mesh.skinWeights[i].weights[1],
                                      mesh.skinWeights[i].weights[2], mesh.skinWeights[i].weights[3]};
    }
    cleanup();
    return result;
}

}  // namespace rigger::render
