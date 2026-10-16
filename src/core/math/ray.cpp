// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/math/ray.hpp"

#include <algorithm>
#include <cmath>

namespace rigger {

Ray Ray::make(const glm::vec3& origin, const glm::vec3& direction) {
    return {origin, glm::normalize(direction)};
}

std::optional<float> intersectSphere(const Ray& ray, const glm::vec3& center, float radius) {
    const glm::vec3 oc = ray.origin - center;
    const float b = glm::dot(oc, ray.dir);
    const float c = glm::dot(oc, oc) - radius * radius;
    if (c <= 0.0f) return 0.0f;  // origine à l'intérieur
    const float disc = b * b - c;
    if (disc < 0.0f) return std::nullopt;
    const float t = -b - std::sqrt(disc);
    if (t < 0.0f) return std::nullopt;  // sphère derrière le rayon
    return t;
}

namespace {
float distanceToSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float h = len2 > 0.0f ? glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * h));
}
}  // namespace

std::optional<float> intersectCapsule(const Ray& ray, const glm::vec3& pa, const glm::vec3& pb,
                                      float radius) {
    if (distanceToSegment(ray.origin, pa, pb) <= radius) return 0.0f;

    std::optional<float> best;
    auto consider = [&best](float t) {
        if (t >= 0.0f && (!best || t < *best)) best = t;
    };

    // Corps cylindrique (hors capuchons)
    const glm::vec3 ba = pb - pa;
    const glm::vec3 oa = ray.origin - pa;
    const float baba = glm::dot(ba, ba);
    const float bard = glm::dot(ba, ray.dir);
    const float baoa = glm::dot(ba, oa);
    const float rdoa = glm::dot(ray.dir, oa);
    const float oaoa = glm::dot(oa, oa);
    const float qa = baba - bard * bard;
    if (baba > 0.0f && qa > 1e-8f * baba) {  // sinon rayon parallèle à l'axe : seuls les capuchons comptent
        const float qb = baba * rdoa - baoa * bard;
        const float qc = baba * oaoa - baoa * baoa - radius * radius * baba;
        const float h = qb * qb - qa * qc;
        if (h >= 0.0f) {
            const float sq = std::sqrt(h);
            for (float t : {(-qb - sq) / qa, (-qb + sq) / qa}) {
                const float y = baoa + t * bard;
                if (y > 0.0f && y < baba) consider(t);
            }
        }
    }
    // Capuchons sphériques
    if (auto t = intersectSphere(ray, pa, radius)) consider(*t);
    if (auto t = intersectSphere(ray, pb, radius)) consider(*t);
    return best;
}

std::optional<TriangleHit> intersectTriangle(const Ray& ray, const glm::vec3& v0, const glm::vec3& v1,
                                             const glm::vec3& v2, bool cullBackface) {
    constexpr float kEps = 1e-8f;
    const glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
    const glm::vec3 p = glm::cross(ray.dir, e2);
    const float det = glm::dot(e1, p);
    if (cullBackface ? det < kEps : std::abs(det) < kEps) return std::nullopt;
    const float inv = 1.0f / det;
    const glm::vec3 tv = ray.origin - v0;
    const float u = glm::dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return std::nullopt;
    const glm::vec3 q = glm::cross(tv, e1);
    const float v = glm::dot(ray.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;
    const float t = glm::dot(e2, q) * inv;
    if (t < 0.0f) return std::nullopt;
    return TriangleHit{t, u, v};
}

Ray screenRay(const glm::mat4& viewProjection, float x, float y, float width, float height) {
    const glm::mat4 inverse = glm::inverse(viewProjection);
    const float ndcX = 2.0f * x / std::max(width, 1.0f) - 1.0f;
    const float ndcY = 1.0f - 2.0f * y / std::max(height, 1.0f);
    const glm::vec4 nearPoint = inverse * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    const glm::vec4 farPoint = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 a = glm::vec3(nearPoint) / nearPoint.w;
    const glm::vec3 b = glm::vec3(farPoint) / farPoint.w;
    return Ray::make(a, b - a);
}

std::optional<float> intersectPlane(const Ray& ray, const glm::vec3& point, const glm::vec3& normal) {
    const float denom = glm::dot(normal, ray.dir);
    if (std::abs(denom) < 1e-8f) return std::nullopt;
    const float t = glm::dot(normal, point - ray.origin) / denom;
    if (t < 0.0f) return std::nullopt;
    return t;
}

std::optional<RayLineClosest> closestRayToLine(const Ray& ray, const glm::vec3& linePoint,
                                               const glm::vec3& lineDir) {
    // Rayon P(s) = o + s d ; droite Q(u) = p + u e (e unitaire). Minimise |P - Q|.
    const glm::vec3 w = ray.origin - linePoint;
    const float b = glm::dot(ray.dir, lineDir);
    const float denom = 1.0f - b * b;        // d et e unitaires
    if (denom < 1e-8f) return std::nullopt;  // parallèles
    const float d = glm::dot(ray.dir, w);
    const float e = glm::dot(lineDir, w);
    const float s = (b * e - d) / denom;
    if (s < 0.0f) return std::nullopt;  // le point le plus proche est derrière la caméra
    const float u = (e - b * d) / denom;
    const glm::vec3 pr = ray.origin + ray.dir * s;
    const glm::vec3 pl = linePoint + lineDir * u;
    return RayLineClosest{s, u, glm::length(pr - pl)};
}

std::optional<float> intersectRing(const Ray& ray, const glm::vec3& center, const glm::vec3& normal,
                                   float radius, float tolerance) {
    const auto t = intersectPlane(ray, center, normal);
    if (!t) return std::nullopt;
    const glm::vec3 p = ray.at(*t) - center;
    const glm::vec3 n = glm::normalize(normal);
    const glm::vec3 inPlane = p - n * glm::dot(p, n);
    if (std::abs(glm::length(inPlane) - radius) > tolerance) return std::nullopt;
    return t;
}

}  // namespace rigger
