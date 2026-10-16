// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "render/gizmo.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace rigger::render {

namespace {

constexpr float kAxisStart = 0.14f;  // début des axes (relatif à size)
constexpr float kAxisEnd = 1.0f;
constexpr float kPickTolerance = 0.08f;  // épaisseur de picking autour des axes
constexpr float kPlaneMin = 0.30f;
constexpr float kPlaneMax = 0.55f;
constexpr float kRingRadius = 0.9f;
constexpr float kRingTolerance = 0.07f;
constexpr float kUniformRadius = 0.16f;
constexpr int kRingSegments = 48;

const glm::vec3 kAxisColors[3] = {{0.92f, 0.25f, 0.25f}, {0.35f, 0.82f, 0.30f}, {0.30f, 0.52f, 1.0f}};
const glm::vec3 kHighlight(1.0f, 0.88f, 0.15f);

GizmoHandle axisHandle(int i) { return static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AxisX) + i); }
GizmoHandle planeHandle(int normalAxis) {
    // PlaneYZ (normale X), PlaneXZ (normale Y), PlaneXY (normale Z).
    return static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::PlaneYZ) + normalAxis);
}
GizmoHandle ringHandle(int i) { return static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::RingX) + i); }

int indexOf(GizmoHandle h) {
    switch (h) {
        case GizmoHandle::AxisX:
        case GizmoHandle::PlaneYZ:
        case GizmoHandle::RingX:
            return 0;
        case GizmoHandle::AxisY:
        case GizmoHandle::PlaneXZ:
        case GizmoHandle::RingY:
            return 1;
        case GizmoHandle::AxisZ:
        case GizmoHandle::PlaneXY:
        case GizmoHandle::RingZ:
            return 2;
        default:
            return -1;
    }
}

bool isAxis(GizmoHandle h) { return h >= GizmoHandle::AxisX && h <= GizmoHandle::AxisZ; }
bool isPlane(GizmoHandle h) { return h >= GizmoHandle::PlaneYZ && h <= GizmoHandle::PlaneXY; }
bool isRing(GizmoHandle h) { return h >= GizmoHandle::RingX && h <= GizmoHandle::RingZ; }

float snapTo(float value, float step) { return std::round(value / step) * step; }

// --- primitives de maillage ---------------------------------------------------------------

std::uint32_t addVertex(GizmoMesh& m, const glm::vec3& p, const glm::vec3& c) {
    m.positions.push_back(p);
    m.colors.push_back(c);
    return static_cast<std::uint32_t>(m.positions.size() - 1);
}

void addTriangle(GizmoMesh& m, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                 const glm::vec3& color) {
    const auto ia = addVertex(m, a, color);
    const auto ib = addVertex(m, b, color);
    const auto ic = addVertex(m, c, color);
    m.indices.insert(m.indices.end(), {ia, ib, ic});
}

void addQuad(GizmoMesh& m, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
             const glm::vec3& color) {
    addTriangle(m, a, b, c, color);
    addTriangle(m, a, c, d, color);
}

// Prisme à base carrée entre a et b (« tube » fin visible sous tous les angles).
void addPrism(GizmoMesh& m, const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec3& color) {
    const glm::vec3 dir = glm::normalize(b - a);
    const glm::vec3 helper = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(dir, helper)) * radius;
    const glm::vec3 v = glm::normalize(glm::cross(dir, u)) * radius;
    const glm::vec3 corners[4] = {u + v, -u + v, -u - v, u - v};
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 c0 = corners[i], c1 = corners[(i + 1) % 4];
        addQuad(m, a + c0, a + c1, b + c1, b + c0, color);
    }
}

void addCone(GizmoMesh& m, const glm::vec3& base, const glm::vec3& tip, float radius,
             const glm::vec3& color) {
    const glm::vec3 dir = glm::normalize(tip - base);
    const glm::vec3 helper = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(dir, helper));
    const glm::vec3 v = glm::cross(dir, u);
    constexpr int kSides = 10;
    for (int i = 0; i < kSides; ++i) {
        const float a0 = glm::two_pi<float>() * static_cast<float>(i) / kSides;
        const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / kSides;
        const glm::vec3 p0 = base + (u * std::cos(a0) + v * std::sin(a0)) * radius;
        const glm::vec3 p1 = base + (u * std::cos(a1) + v * std::sin(a1)) * radius;
        addTriangle(m, p0, p1, tip, color);
        addTriangle(m, p0, p1, base, color);  // fond
    }
}

void addCube(GizmoMesh& m, const glm::vec3& center, const glm::vec3& axis, float half,
             const glm::vec3& color) {
    const glm::vec3 a = glm::normalize(axis);
    const glm::vec3 helper = std::abs(a.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(a, helper)) * half;
    const glm::vec3 v = glm::normalize(glm::cross(a, u)) * half;
    const glm::vec3 w = a * half;
    addPrism(m, center - w, center + w, half, color);
    addQuad(m, center + w + u + v, center + w - u + v, center + w - u - v, center + w + u - v, color);
    addQuad(m, center - w + u + v, center - w - u + v, center - w - u - v, center - w + u - v, color);
}

}  // namespace

void Gizmo::setFrame(const glm::vec3& origin, const glm::quat& orientation, float size) {
    origin_ = origin;
    orientation_ = glm::normalize(orientation);
    size_ = std::max(size, 1e-4f);
}

glm::vec3 Gizmo::axis(int index) const {
    glm::vec3 unit(0.0f);
    unit[index] = 1.0f;
    return orientation_ * unit;
}

float Gizmo::constantScreenSize(float distance, float fovY, float screenFraction) {
    return 2.0f * distance * std::tan(fovY * 0.5f) * screenFraction;
}

glm::vec3 Gizmo::handleAxis(GizmoHandle handle) const { return axis(indexOf(handle)); }
glm::vec3 Gizmo::handlePlaneNormal(GizmoHandle handle) const { return axis(indexOf(handle)); }

glm::vec3 Gizmo::handleColor(GizmoHandle handle, int axisIndex) const {
    if (handle == hover_ || handle == active_) {
        return kHighlight;
    }
    return axisIndex >= 0 ? kAxisColors[axisIndex] : glm::vec3(0.85f);
}

// --- picking ----------------------------------------------------------------------------

GizmoHandle Gizmo::pick(const Ray& ray) const {
    GizmoHandle best = GizmoHandle::None;
    float bestT = 1e30f;
    const auto consider = [&](GizmoHandle h, float t) {
        if (t < bestT) {
            bestT = t;
            best = h;
        }
    };
    const float tolerance = kPickTolerance * size_;

    if (mode_ == GizmoMode::Rotate) {
        for (int i = 0; i < 3; ++i) {
            if (const auto t =
                    intersectRing(ray, origin_, axis(i), kRingRadius * size_, kRingTolerance * size_)) {
                consider(ringHandle(i), *t);
            }
        }
        return best;
    }

    if (mode_ == GizmoMode::Scale) {
        if (const auto t = intersectSphere(ray, origin_, kUniformRadius * size_)) {
            return GizmoHandle::Uniform;
        }
    }
    const float start = (mode_ == GizmoMode::Scale ? 0.15f : 0.1f) * size_;
    const float end = 1.1f * size_;
    for (int i = 0; i < 3; ++i) {
        const auto hit = closestRayToLine(ray, origin_, axis(i));
        if (hit && hit->tLine >= start && hit->tLine <= end && hit->distance <= tolerance) {
            consider(axisHandle(i), hit->tRay);
        }
    }
    if (best != GizmoHandle::None || mode_ != GizmoMode::Translate) {
        return best;
    }
    // Carrés de translation (seulement si aucun axe n'est touché).
    for (int n = 0; n < 3; ++n) {
        const auto t = intersectPlane(ray, origin_, axis(n));
        if (!t) {
            continue;
        }
        const glm::vec3 local = ray.at(*t) - origin_;
        const float u = glm::dot(local, axis((n + 1) % 3)) / size_;
        const float v = glm::dot(local, axis((n + 2) % 3)) / size_;
        if (u >= kPlaneMin && u <= kPlaneMax && v >= kPlaneMin && v <= kPlaneMax) {
            consider(planeHandle(n), *t);
        }
    }
    return best;
}

// --- glissé -----------------------------------------------------------------------------

bool Gizmo::beginDrag(GizmoHandle handle, const Ray& startRay, const glm::vec3& cameraForward) {
    active_ = GizmoHandle::None;
    cameraForward_ = cameraForward;
    accumulatedAngle_ = 0.0f;
    lastRawAngle_ = 0.0f;
    if (isAxis(handle)) {
        const auto hit = closestRayToLine(startRay, origin_, handleAxis(handle));
        if (!hit) {
            return false;
        }
        startParam_ = hit->tLine;
        if (mode_ == GizmoMode::Scale) {  // évite un facteur énorme quand on attrape l'axe près de l'origine
            startParam_ =
                (startParam_ < 0.0f ? -1.0f : 1.0f) * std::max(std::abs(startParam_), 0.25f * size_);
        }
    } else if (isPlane(handle)) {
        const auto t = intersectPlane(startRay, origin_, handlePlaneNormal(handle));
        if (!t) {
            return false;
        }
        startPoint_ = startRay.at(*t);
    } else if (isRing(handle)) {
        const glm::vec3 n = handleAxis(handle);
        const auto t = intersectPlane(startRay, origin_, n);
        if (!t) {
            return false;
        }
        glm::vec3 v = startRay.at(*t) - origin_;
        v -= n * glm::dot(v, n);
        if (glm::length(v) < 1e-6f) {
            return false;
        }
        startVector_ = glm::normalize(v);
    } else if (handle == GizmoHandle::Uniform) {
        const auto t = intersectPlane(startRay, origin_, cameraForward);
        if (!t) {
            return false;
        }
        startParam_ = std::max(glm::length(startRay.at(*t) - origin_), 0.1f * size_);
    } else {
        return false;
    }
    active_ = handle;
    return true;
}

WorldDelta Gizmo::drag(const Ray& ray, bool snap) {
    WorldDelta delta;
    delta.scaleBasis = orientation_;
    if (!isDragging()) {
        return delta;
    }
    if (isAxis(active_)) {
        const glm::vec3 a = handleAxis(active_);
        const auto hit = closestRayToLine(ray, origin_, a);
        if (!hit) {
            return delta;
        }
        if (mode_ == GizmoMode::Scale) {
            float factor = hit->tLine / startParam_;
            factor = std::max(factor, 0.01f);
            if (snap) {
                factor = std::max(snapTo(factor, kSnapScale), kSnapScale);
            }
            delta.scale = glm::vec3(1.0f);
            delta.scale[indexOf(active_)] = factor;
        } else {
            float d = hit->tLine - startParam_;
            if (snap) {
                d = snapTo(d, kSnapTranslation);
            }
            delta.translation = a * d;
        }
    } else if (isPlane(active_)) {
        const glm::vec3 n = handlePlaneNormal(active_);
        const auto t = intersectPlane(ray, origin_, n);
        if (!t) {
            return delta;
        }
        glm::vec3 d = ray.at(*t) - startPoint_;
        if (snap) {
            const int i = indexOf(active_);
            const glm::vec3 a1 = axis((i + 1) % 3), a2 = axis((i + 2) % 3);
            d = a1 * snapTo(glm::dot(d, a1), kSnapTranslation) +
                a2 * snapTo(glm::dot(d, a2), kSnapTranslation);
        }
        delta.translation = d;
    } else if (isRing(active_)) {
        const glm::vec3 n = handleAxis(active_);
        const auto t = intersectPlane(ray, origin_, n);
        if (!t) {
            return delta;
        }
        glm::vec3 v = ray.at(*t) - origin_;
        v -= n * glm::dot(v, n);
        if (glm::length(v) < 1e-6f) {
            return delta;
        }
        v = glm::normalize(v);
        const float raw = std::atan2(glm::dot(n, glm::cross(startVector_, v)), glm::dot(startVector_, v));
        float step = raw - lastRawAngle_;
        if (step > glm::pi<float>()) {
            step -= glm::two_pi<float>();
        } else if (step < -glm::pi<float>()) {
            step += glm::two_pi<float>();
        }
        accumulatedAngle_ += step;  // déroule l'angle : le glissé peut dépasser un demi-tour
        lastRawAngle_ = raw;
        float angle = accumulatedAngle_;
        if (snap) {
            angle = snapTo(angle, kSnapRotation);
        }
        delta.rotation = glm::angleAxis(angle, n);
    } else if (active_ == GizmoHandle::Uniform) {
        const auto t = intersectPlane(ray, origin_, cameraForward_);
        if (!t) {
            return delta;
        }
        float factor = std::max(glm::length(ray.at(*t) - origin_) / startParam_, 0.01f);
        if (snap) {
            factor = std::max(snapTo(factor, kSnapScale), kSnapScale);
        }
        delta.scale = glm::vec3(factor);
    }
    return delta;
}

// --- maillage ---------------------------------------------------------------------------

GizmoMesh Gizmo::buildMesh() const {
    GizmoMesh m;
    const float s = size_;
    if (mode_ == GizmoMode::Rotate) {
        for (int i = 0; i < 3; ++i) {
            const glm::vec3 n = axis(i);
            const glm::vec3 u = axis((i + 1) % 3), v = axis((i + 2) % 3);
            const glm::vec3 color = handleColor(ringHandle(i), i);
            const float radius = kRingRadius * s;
            for (int k = 0; k < kRingSegments; ++k) {
                const float a0 = glm::two_pi<float>() * static_cast<float>(k) / kRingSegments;
                const float a1 = glm::two_pi<float>() * static_cast<float>(k + 1) / kRingSegments;
                const glm::vec3 p0 = origin_ + (u * std::cos(a0) + v * std::sin(a0)) * radius;
                const glm::vec3 p1 = origin_ + (u * std::cos(a1) + v * std::sin(a1)) * radius;
                addPrism(m, p0, p1, 0.014f * s, color);
            }
            (void)n;
        }
        return m;
    }

    for (int i = 0; i < 3; ++i) {
        const glm::vec3 a = axis(i);
        const glm::vec3 color = handleColor(axisHandle(i), i);
        const float shaftEnd = mode_ == GizmoMode::Translate ? 0.80f : 0.84f;
        addPrism(m, origin_ + a * (kAxisStart * s), origin_ + a * (shaftEnd * s), 0.018f * s, color);
        if (mode_ == GizmoMode::Translate) {
            addCone(m, origin_ + a * (shaftEnd * s), origin_ + a * (kAxisEnd * s), 0.07f * s, color);
        } else {
            addCube(m, origin_ + a * (0.92f * s), a, 0.07f * s, color);
        }
    }
    if (mode_ == GizmoMode::Translate) {
        for (int n = 0; n < 3; ++n) {
            const glm::vec3 u = axis((n + 1) % 3), v = axis((n + 2) % 3);
            const glm::vec3 color = handleColor(planeHandle(n), n) * 0.9f;
            const glm::vec3 p0 = origin_ + (u * kPlaneMin + v * kPlaneMin) * s;
            const glm::vec3 p1 = origin_ + (u * kPlaneMax + v * kPlaneMin) * s;
            const glm::vec3 p2 = origin_ + (u * kPlaneMax + v * kPlaneMax) * s;
            const glm::vec3 p3 = origin_ + (u * kPlaneMin + v * kPlaneMax) * s;
            addQuad(m, p0, p1, p2, p3, color);
        }
    } else {
        addCube(m, origin_, axis(1), kUniformRadius * s * 0.8f, handleColor(GizmoHandle::Uniform, -1));
    }
    return m;
}

}  // namespace rigger::render
