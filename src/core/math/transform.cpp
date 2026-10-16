// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/math/transform.hpp"

#include <cassert>
#include <cmath>

#include "core/math/quat_utils.hpp"

namespace rigger {

glm::mat4 Transform::toMat4() const {
    const glm::mat3 rot = glm::mat3_cast(r);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(rot[0] * s.x, 0.0f);
    m[1] = glm::vec4(rot[1] * s.y, 0.0f);
    m[2] = glm::vec4(rot[2] * s.z, 0.0f);
    m[3] = glm::vec4(t, 1.0f);
    return m;
}

Transform Transform::fromMat4(const glm::mat4& m) {
    Transform out;
    out.t = glm::vec3(m[3]);

    glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
    out.s = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
    if (glm::determinant(glm::mat3(m)) < 0.0f) out.s.x = -out.s.x;  // miroir

    constexpr float kEps = 1e-8f;
    c0 = std::abs(out.s.x) > kEps ? c0 / out.s.x : glm::vec3(1, 0, 0);
    c1 = std::abs(out.s.y) > kEps ? c1 / out.s.y : glm::vec3(0, 1, 0);
    c2 = std::abs(out.s.z) > kEps ? c2 / out.s.z : glm::vec3(0, 0, 1);
    out.r = glm::normalize(glm::quat_cast(glm::mat3(c0, c1, c2)));
    return out;
}

Transform Transform::inverse() const {
    [[maybe_unused]] constexpr float kEps = 1e-12f;  // utilisé uniquement par l'assert (absent en Release)
    assert(std::abs(s.x) > kEps && std::abs(s.y) > kEps && std::abs(s.z) > kEps);
    Transform inv;
    inv.s = 1.0f / s;
    inv.r = glm::conjugate(r);
    inv.t = -(inv.r * (inv.s * t));
    return inv;
}

Transform Transform::operator*(const Transform& child) const {
    Transform out;
    out.t = t + r * (s * child.t);
    out.r = glm::normalize(r * child.r);
    out.s = s * child.s;
    return out;
}

Transform lerp(const Transform& a, const Transform& b, float u) {
    Transform out;
    out.t = glm::mix(a.t, b.t, u);
    out.r = slerpShortest(a.r, b.r, u);
    out.s = glm::mix(a.s, b.s, u);
    return out;
}

}  // namespace rigger
