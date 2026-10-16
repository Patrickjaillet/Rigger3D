// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/buffer.hpp"

#include <utility>

namespace rigger::render {

VertexArray::VertexArray() { glGenVertexArrays(1, &id_); }

VertexArray::VertexArray(VertexArray&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteVertexArrays(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

VertexArray::~VertexArray() {
    if (id_ != 0) {
        glDeleteVertexArrays(1, &id_);
    }
}

void VertexArray::bind() const {
    if (id_ != 0) {
        glBindVertexArray(id_);
    }
}

void VertexArray::unbind() const { glBindVertexArray(0); }

void VertexArray::setAttrib(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                            const void* offset) const {
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, size, type, normalized, stride, offset);
}

VertexBuffer::VertexBuffer() { glGenBuffers(1, &id_); }

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteBuffers(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

VertexBuffer::~VertexBuffer() {
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
    }
}

void VertexBuffer::bind() const {
    if (id_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, id_);
    }
}

void VertexBuffer::unbind() const { glBindBuffer(GL_ARRAY_BUFFER, 0); }

void VertexBuffer::setData(GLsizeiptr size, const void* data, GLenum usage) const {
    bind();
    glBufferData(GL_ARRAY_BUFFER, size, data, usage);
}

IndexBuffer::IndexBuffer() { glGenBuffers(1, &id_); }

IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteBuffers(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

IndexBuffer::~IndexBuffer() {
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
    }
}

void IndexBuffer::bind() const {
    if (id_ != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id_);
    }
}

void IndexBuffer::unbind() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }

void IndexBuffer::setData(GLsizeiptr size, const void* data, GLenum usage) const {
    bind();
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, usage);
}

UniformBuffer::UniformBuffer() { glGenBuffers(1, &id_); }

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteBuffers(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

UniformBuffer::~UniformBuffer() {
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
    }
}

void UniformBuffer::bind() const {
    if (id_ != 0) {
        glBindBuffer(GL_UNIFORM_BUFFER, id_);
    }
}

void UniformBuffer::unbind() const { glBindBuffer(GL_UNIFORM_BUFFER, 0); }

void UniformBuffer::bindBase(GLuint index) const {
    if (id_ != 0) {
        glBindBufferBase(GL_UNIFORM_BUFFER, index, id_);
    }
}

void UniformBuffer::setData(GLsizeiptr size, const void* data, GLenum usage) const {
    bind();
    glBufferData(GL_UNIFORM_BUFFER, size, data, usage);
}

Framebuffer::Framebuffer() { glGenFramebuffers(1, &id_); }

Framebuffer::Framebuffer(Framebuffer&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteFramebuffers(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

Framebuffer::~Framebuffer() {
    if (id_ != 0) {
        glDeleteFramebuffers(1, &id_);
    }
}

void Framebuffer::bind() const {
    if (id_ != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, id_);
    }
}

void Framebuffer::unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

bool Framebuffer::isComplete() const {
    if (id_ == 0) {
        return false;
    }
    bind();
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    unbind();
    return complete;
}

void Framebuffer::attachColorTexture(GLuint texture, GLenum attachment) const {
    if (id_ == 0) {
        return;
    }
    bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture, 0);
    unbind();
}

void Framebuffer::attachDepthTexture(GLuint texture) const {
    if (id_ == 0) {
        return;
    }
    bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    unbind();
}

void Framebuffer::setDrawBuffers(const std::vector<GLenum>& attachments) const {
    if (id_ == 0 || attachments.empty()) {
        return;
    }
    bind();
    glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
    unbind();
}

}  // namespace rigger::render
