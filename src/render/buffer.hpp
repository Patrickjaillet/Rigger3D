// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

#include "render/glad_include.hpp"

#include <vector>

namespace rigger::render {

class VertexArray {
public:
    VertexArray();
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;
    ~VertexArray();

    void bind() const;
    void unbind() const;
    void setAttrib(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                   const void* offset) const;
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class VertexBuffer {
public:
    VertexBuffer();
    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;
    VertexBuffer(VertexBuffer&& other) noexcept;
    VertexBuffer& operator=(VertexBuffer&& other) noexcept;
    ~VertexBuffer();

    void bind() const;
    void unbind() const;
    void setData(GLsizeiptr size, const void* data, GLenum usage = GL_STATIC_DRAW) const;
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class IndexBuffer {
public:
    IndexBuffer();
    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;
    IndexBuffer(IndexBuffer&& other) noexcept;
    IndexBuffer& operator=(IndexBuffer&& other) noexcept;
    ~IndexBuffer();

    void bind() const;
    void unbind() const;
    void setData(GLsizeiptr size, const void* data, GLenum usage = GL_STATIC_DRAW) const;
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class UniformBuffer {
public:
    UniformBuffer();
    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;
    UniformBuffer(UniformBuffer&& other) noexcept;
    UniformBuffer& operator=(UniformBuffer&& other) noexcept;
    ~UniformBuffer();

    void bind() const;
    void unbind() const;
    void bindBase(GLuint index) const;
    void setData(GLsizeiptr size, const void* data, GLenum usage = GL_STATIC_DRAW) const;
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class Framebuffer {
public:
    Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;
    ~Framebuffer();

    void bind() const;
    void unbind() const;
    bool isComplete() const;
    void attachColorTexture(GLuint texture, GLenum attachment = GL_COLOR_ATTACHMENT0) const;
    void attachDepthTexture(GLuint texture) const;
    void setDrawBuffers(const std::vector<GLenum>& attachments) const;
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

}  // namespace rigger::render
