// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "core/math/ray.hpp"
#include "core/skeleton/manipulate.hpp"

namespace rigger::render {

// Gizmo de manipulation maison (translation / rotation / échelle), indépendant de Qt et d'OpenGL : cette
// classe ne fait que de la géométrie (picking par rayon, calcul du glissé, construction du maillage à
// dessiner).
//
// Le gizmo vit dans le repère donné par setFrame : origine monde, orientation des axes (Local / Parent /
// Global selon GizmoSpace, choisie par l'appelant) et `size`, rayon monde des axes. Pour une taille constante
// à l'écran, l'appelant passe size = distance_caméra * facteur (voir constantScreenSize).
enum class GizmoMode { Translate, Rotate, Scale };

enum class GizmoHandle {
    None,
    AxisX,
    AxisY,
    AxisZ,
    PlaneYZ,  // carré de translation dont la normale est X (déplace en Y et Z)
    PlaneXZ,
    PlaneXY,
    RingX,  // arcs de rotation autour de X / Y / Z
    RingY,
    RingZ,
    Uniform  // échelle uniforme (centre)
};

struct GizmoMesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> colors;
    std::vector<std::uint32_t> indices;  // triangles
};

class Gizmo {
public:
    static constexpr float kSnapTranslation = 0.1f;                       // m
    static constexpr float kSnapRotation = 15.0f * 3.14159265f / 180.0f;  // rad
    static constexpr float kSnapScale = 0.1f;

    void setMode(GizmoMode mode) { mode_ = mode; }
    GizmoMode mode() const { return mode_; }
    void setFrame(const glm::vec3& origin, const glm::quat& orientation, float size);
    const glm::vec3& origin() const { return origin_; }
    const glm::quat& orientation() const { return orientation_; }
    float size() const { return size_; }
    glm::vec3 axis(int index) const;  // axe monde 0 = X, 1 = Y, 2 = Z

    // Taille monde donnant `screenFraction` de la hauteur de la vue à la distance `distance` (perspective).
    static float constantScreenSize(float distance, float fovY, float screenFraction = 0.12f);

    void setHover(GizmoHandle handle) { hover_ = handle; }
    GizmoHandle hover() const { return hover_; }

    // Poignée touchée par le rayon (la plus proche), None sinon.
    GizmoHandle pick(const Ray& ray) const;

    // Glissé. `cameraForward` sert à l'échelle uniforme (plan face à la caméra). Renvoie false si le rayon ne
    // permet pas de démarrer (parallèle au plan / à l'axe de la poignée).
    bool beginDrag(GizmoHandle handle, const Ray& startRay, const glm::vec3& cameraForward);
    bool isDragging() const { return active_ != GizmoHandle::None; }
    GizmoHandle activeHandle() const { return active_; }
    // Delta TOTAL depuis le début du glissé (absolu, pas incrémental). Avec `snap` : pas de 0,1 m / 15° /
    // 0,1.
    WorldDelta drag(const Ray& ray, bool snap);
    void endDrag() { active_ = GizmoHandle::None; }
    // Angle cumulé (radians) du glissé de rotation en cours, pour l'affichage.
    float rotationAngle() const { return accumulatedAngle_; }

    // Maillage à dessiner (triangles, couleurs par axe ; survol et poignée active en jaune).
    GizmoMesh buildMesh() const;

private:
    glm::vec3 handleAxis(GizmoHandle handle) const;
    glm::vec3 handlePlaneNormal(GizmoHandle handle) const;
    glm::vec3 handleColor(GizmoHandle handle, int axisIndex) const;

    GizmoMode mode_ = GizmoMode::Translate;
    glm::vec3 origin_{0.0f};
    glm::quat orientation_{1.0f, 0.0f, 0.0f, 0.0f};
    float size_ = 1.0f;
    GizmoHandle hover_ = GizmoHandle::None;

    // État du glissé.
    GizmoHandle active_ = GizmoHandle::None;
    glm::vec3 startPoint_{0.0f};  // point de départ sur l'axe / le plan
    float startParam_ = 0.0f;
    glm::vec3 startVector_{0.0f};
    glm::vec3 cameraForward_{0.0f, 0.0f, -1.0f};
    float lastRawAngle_ = 0.0f;
    float accumulatedAngle_ = 0.0f;
};

}  // namespace rigger::render
