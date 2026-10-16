// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/camera.hpp"

#include <algorithm>
#include <cmath>

namespace rigger::render {

namespace {

constexpr float kMinPitch = -89.0f * glm::pi<float>() / 180.0f;
constexpr float kMaxPitch = 89.0f * glm::pi<float>() / 180.0f;

}  // namespace

glm::vec3 Camera::position() const {
    const float x = std::cos(pitch) * std::sin(yaw);
    const float y = std::sin(pitch);
    const float z = std::cos(pitch) * std::cos(yaw);
    return target + distance * glm::vec3(x, y, z);
}

void Camera::orbit(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw;
    pitch = std::clamp(pitch + deltaPitch, kMinPitch, kMaxPitch);
}

void Camera::pan(float deltaX, float deltaY) {
    const glm::vec3 cameraToTarget = position() - target;
    const glm::vec3 forward = glm::normalize(cameraToTarget);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    if (glm::dot(right, right) < 1e-6f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    target += right * deltaX + up * deltaY;
}

void Camera::zoom(float delta) { distance = std::clamp(distance + delta, 0.5f, 100.0f); }

void Camera::focusOn(const glm::vec3& center, float radius) {
    target = center;
    distance = std::max(3.0f, radius * 2.5f);
    pitch = glm::radians(30.0f);
    yaw = glm::radians(45.0f);
    orthographic = false;
}

glm::mat4 Camera::viewMatrix() const { return glm::lookAt(position(), target, worldUp); }

glm::mat4 Camera::projectionMatrix(float aspectRatio) const {
    if (orthographic) {
        const float halfHeight = orthographicSize * 0.5f;
        const float halfWidth = halfHeight * aspectRatio;
        return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    }
    return glm::perspective(fovY, aspectRatio, nearPlane, farPlane);
}

}  // namespace rigger::render
