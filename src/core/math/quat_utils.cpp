// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/math/quat_utils.hpp"

#include <cmath>
#include <glm/gtc/constants.hpp>

namespace rigger {

glm::quat fromTo(const glm::vec3& a, const glm::vec3& b) {
    const float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-8f || lb < 1e-8f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 u = a / la, v = b / lb;
    const float d = glm::dot(u, v);
    if (d > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.999999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), u);
        if (glm::length(axis) < 1e-4f) axis = glm::cross(glm::vec3(0, 1, 0), u);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    const glm::vec3 c = glm::cross(u, v);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::quat slerpShortest(const glm::quat& a, const glm::quat& b, float u) {
    glm::quat bb = b;
    float d = glm::dot(a, bb);
    if (d < 0.0f) {
        bb = -bb;
        d = -d;
    }
    if (d > 0.9995f) return glm::normalize(a * (1.0f - u) + bb * u);  // quasi identiques : nlerp
    const float theta = std::acos(d);
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0f - u) * theta) / sinTheta;
    const float wb = std::sin(u * theta) / sinTheta;
    return glm::normalize(a * wa + bb * wb);
}

SwingTwist swingTwist(const glm::quat& q, const glm::vec3& axis) {
    const glm::vec3 ax = glm::normalize(axis);
    const glm::vec3 v(q.x, q.y, q.z);
    const glm::vec3 proj = ax * glm::dot(v, ax);
    glm::quat twist(q.w, proj);
    const float len = std::sqrt(glm::dot(twist, twist));
    twist = len < 1e-8f ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : twist * (1.0f / len);
    return {glm::normalize(q * glm::conjugate(twist)), twist};
}

glm::quat fromEulerXYZ(const glm::vec3& e) {
    return glm::angleAxis(e.z, glm::vec3(0, 0, 1)) * glm::angleAxis(e.y, glm::vec3(0, 1, 0)) *
           glm::angleAxis(e.x, glm::vec3(1, 0, 0));
}

glm::vec3 toEulerXYZ(const glm::quat& q) {
    // m[col][row] ; pour R = Rz*Ry*Rx : R(2,0) = -sin y
    const glm::mat3 m = glm::mat3_cast(glm::normalize(q));
    const float sy = glm::clamp(-m[0][2], -1.0f, 1.0f);
    glm::vec3 e;
    e.y = std::asin(sy);
    if (std::abs(sy) > 0.999999f) {  // blocage de cardan : on met z à 0
        e.z = 0.0f;
        e.x = std::atan2(sy > 0.0f ? m[1][0] : -m[1][0], m[1][1]);
    } else {
        e.x = std::atan2(m[1][2], m[2][2]);
        e.z = std::atan2(m[0][1], m[0][0]);
    }
    return e;
}

bool sameRotation(const glm::quat& a, const glm::quat& b, float eps) {
    return std::abs(glm::dot(a, b)) > 1.0f - eps;
}

}  // namespace rigger
