// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <doctest/doctest.h>

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <string>

namespace testutil {

inline bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
}
inline bool near(const glm::mat4& a, const glm::mat4& b, float eps = 2e-4f) {
    for (int c = 0; c < 4; ++c)
        if (!near(glm::vec3(a[c]), glm::vec3(b[c]), eps) || std::abs(a[c][3] - b[c][3]) > eps) return false;
    return true;
}
inline bool sameRot(const glm::quat& a, const glm::quat& b, float eps = 1e-5f) {
    return std::abs(glm::dot(a, b)) > 1.0f - eps;
}
inline std::string str(const glm::vec3& v) {
    return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
}

// Générateur pseudo-aléatoire déterministe (graine fixe : tests reproductibles).
class Rng {
public:
    explicit Rng(unsigned seed = 1234) : gen_(seed) {}
    float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(gen_); }
    int integer(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(gen_); }
    glm::vec3 vec(float range = 5.0f) {
        return {uniform(-range, range), uniform(-range, range), uniform(-range, range)};
    }
    glm::vec3 unit() {
        glm::vec3 v;
        do {
            v = vec(1.0f);
        } while (glm::dot(v, v) < 1e-3f);
        return glm::normalize(v);
    }
    glm::quat quat() { return glm::angleAxis(uniform(-glm::pi<float>(), glm::pi<float>()), unit()); }

private:
    std::mt19937 gen_;
};

}  // namespace testutil

#define CHECK_VEC3(a, b) \
    CHECK_MESSAGE(testutil::near((a), (b)), "got ", testutil::str(a), " expected ", testutil::str(b))
