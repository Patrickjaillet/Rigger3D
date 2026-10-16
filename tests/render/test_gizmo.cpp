// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include <algorithm>

#include "render/gizmo.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using namespace rigger::render;
using testutil::near;
using testutil::sameRot;

namespace {

// Rayon qui part de (x, y, 10) et va vers -Z : « le curseur est au-dessus du point (x, y) ».
Ray down(float x, float y) { return Ray::make({x, y, 10.0f}, {0, 0, -1}); }

Gizmo makeGizmo(GizmoMode mode, const glm::quat& orientation = glm::quat(1, 0, 0, 0)) {
    Gizmo g;
    g.setMode(mode);
    g.setFrame({0, 0, 0}, orientation, 1.0f);
    return g;
}

}  // namespace

TEST_CASE("gizmo : taille constante à l'écran = proportionnelle à la distance") {
    const float fov = glm::radians(45.0f);
    const float near5 = Gizmo::constantScreenSize(5.0f, fov);
    CHECK(Gizmo::constantScreenSize(10.0f, fov) == doctest::Approx(2.0f * near5));
    CHECK(near5 > 0.0f);
}

TEST_CASE("gizmo translation : picking des axes, des carrés et du vide") {
    const Gizmo g = makeGizmo(GizmoMode::Translate);
    CHECK(g.pick(Ray::make({0.5f, 0, 10}, {0, 0, -1})) == GizmoHandle::AxisX);
    CHECK(g.pick(Ray::make({0, 0.5f, 10}, {0, 0, -1})) == GizmoHandle::AxisY);
    // Axe Z vu de face (le long de l'axe) : on le touche en visant le centre, depuis le côté on le voit en
    // profondeur.
    CHECK(g.pick(Ray::make({10, 0, 0.5f}, {-1, 0, 0})) == GizmoHandle::AxisZ);
    CHECK(g.pick(down(0.4f, 0.4f)) == GizmoHandle::PlaneXY);  // carré de normale Z
    CHECK(g.pick(Ray::make({10, 0.4f, 0.4f}, {-1, 0, 0})) == GizmoHandle::PlaneYZ);
    CHECK(g.pick(Ray::make({0.4f, 10, 0.4f}, {0, -1, 0})) == GizmoHandle::PlaneXZ);
    CHECK(g.pick(down(2.5f, 2.5f)) == GizmoHandle::None);  // dans le vide
    CHECK(g.pick(down(0.5f, 0.2f)) == GizmoHandle::None);  // trop loin de l axe X (tolérance 0,08)
    CHECK(g.pick(down(1.5f, 0.0f)) == GizmoHandle::None);  // au-delà de l'extrémité
}

TEST_CASE("gizmo : l'orientation tourne les axes (repère local / parent)") {
    // Gizmo tourné de 90° autour de Z : son axe X pointe vers le Y monde.
    const Gizmo g = makeGizmo(GizmoMode::Translate, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)));
    CHECK(near(g.axis(0), {0, 1, 0}, 1e-5f));
    CHECK(g.pick(down(0.0f, 0.5f)) == GizmoHandle::AxisX);
    CHECK(g.pick(down(-0.5f, 0.0f)) == GizmoHandle::AxisY);
}

TEST_CASE("gizmo rotation : picking des anneaux") {
    const Gizmo g = makeGizmo(GizmoMode::Rotate);
    CHECK(g.pick(down(0.9f, 0.0f)) == GizmoHandle::RingZ);
    CHECK(g.pick(down(0.0f, -0.9f)) == GizmoHandle::RingZ);
    CHECK(g.pick(down(0.0f, 0.0f)) == GizmoHandle::None);                       // centre
    CHECK(g.pick(down(1.3f, 0.0f)) == GizmoHandle::None);                       // hors de l'anneau
    CHECK(g.pick(Ray::make({10, 0.9f, 0}, {-1, 0, 0})) == GizmoHandle::RingX);  // plan x = 0
    CHECK(g.pick(Ray::make({0.9f, 10, 0}, {0, -1, 0})) == GizmoHandle::RingY);
}

TEST_CASE("gizmo échelle : centre uniforme prioritaire, axes aux cubes") {
    const Gizmo g = makeGizmo(GizmoMode::Scale);
    CHECK(g.pick(down(0.0f, 0.0f)) == GizmoHandle::Uniform);
    CHECK(g.pick(down(0.6f, 0.0f)) == GizmoHandle::AxisX);
    CHECK(g.pick(down(0.0f, 0.6f)) == GizmoHandle::AxisY);
    CHECK(g.pick(down(0.4f, 0.4f)) == GizmoHandle::None);  // pas de carrés en mode échelle
}

TEST_CASE("glissé de translation sur un axe : delta absolu, pas de dérive, snap à 0,1 m") {
    Gizmo g = makeGizmo(GizmoMode::Translate);
    REQUIRE(g.beginDrag(GizmoHandle::AxisX, down(0.5f, 0.02f), {0, 0, -1}));
    CHECK(g.isDragging());
    CHECK(near(g.drag(down(1.5f, 0.02f), false).translation, {1, 0, 0}, 1e-4f));
    CHECK(near(g.drag(down(1.5f, 0.02f), false).translation, {1, 0, 0}, 1e-4f));  // idempotent : delta TOTAL
    CHECK(near(g.drag(down(0.5f, 0.3f), false).translation, {0, 0, 0}, 1e-4f));   // seul l'axe compte
    CHECK(near(g.drag(down(1.47f, 0.0f), true).translation, {1.0f, 0, 0}, 1e-4f));  // 0,97 -> 1,0
    CHECK(near(g.drag(down(1.44f, 0.0f), true).translation, {0.9f, 0, 0}, 1e-4f));  // 0,94 -> 0,9
    g.endDrag();
    CHECK_FALSE(g.isDragging());
    CHECK(g.drag(down(2, 0), false).isIdentity());  // plus de glissé : delta nul
}

TEST_CASE("glissé de translation dans un plan : suit le curseur, snap par composante") {
    Gizmo g = makeGizmo(GizmoMode::Translate);
    REQUIRE(g.beginDrag(GizmoHandle::PlaneXY, down(0.4f, 0.4f), {0, 0, -1}));
    const WorldDelta d = g.drag(down(1.4f, -0.6f), false);
    CHECK(near(d.translation, {1.0f, -1.0f, 0}, 1e-4f));
    const WorldDelta snapped = g.drag(down(1.37f, -0.62f), true);
    CHECK(near(snapped.translation, {1.0f, -1.0f, 0}, 1e-4f));
}

TEST_CASE("glissé de rotation : angle signé, snap à 15°, déroulement au-delà de 180°") {
    Gizmo g = makeGizmo(GizmoMode::Rotate);
    REQUIRE(g.beginDrag(GizmoHandle::RingZ, down(0.9f, 0.0f), {0, 0, -1}));
    const WorldDelta quarter = g.drag(down(0.0f, 0.9f), false);
    CHECK(sameRot(quarter.rotation, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1))));
    CHECK(g.rotationAngle() == doctest::Approx(glm::radians(90.0f)).epsilon(1e-4));

    // 80° -> snap 75°.
    const float a = glm::radians(80.0f);
    const WorldDelta snapped = g.drag(down(0.9f * std::cos(a), 0.9f * std::sin(a)), true);
    CHECK(sameRot(snapped.rotation, glm::angleAxis(glm::radians(75.0f), glm::vec3(0, 0, 1))));

    // Déroulement : 170° puis 190° (= -170° brut) : l'angle continue d'augmenter.
    for (const float deg : {120.0f, 170.0f, 190.0f}) {
        const float r = glm::radians(deg);
        g.drag(down(0.9f * std::cos(r), 0.9f * std::sin(r)), false);
    }
    CHECK(g.rotationAngle() == doctest::Approx(glm::radians(190.0f)).epsilon(1e-3));
    // Sens inverse : angle négatif.
    REQUIRE(g.beginDrag(GizmoHandle::RingZ, down(0.9f, 0.0f), {0, 0, -1}));
    const WorldDelta back = g.drag(down(0.0f, -0.9f), false);
    CHECK(sameRot(back.rotation, glm::angleAxis(glm::radians(-90.0f), glm::vec3(0, 0, 1))));
}

TEST_CASE("glissé d'échelle : facteur par axe et uniforme") {
    Gizmo g = makeGizmo(GizmoMode::Scale);
    REQUIRE(g.beginDrag(GizmoHandle::AxisX, down(0.5f, 0.0f), {0, 0, -1}));
    WorldDelta d = g.drag(down(1.0f, 0.0f), false);
    CHECK(near(d.scale, {2.0f, 1.0f, 1.0f}, 1e-4f));
    d = g.drag(down(1.04f, 0.0f), true);  // 2,08 -> 2,1
    CHECK(near(d.scale, {2.1f, 1.0f, 1.0f}, 1e-4f));
    d = g.drag(down(-0.5f, 0.0f), false);  // ne passe pas sous un facteur minimal
    CHECK(d.scale.x >= 0.01f);
    CHECK(sameRot(d.scaleBasis, glm::quat(1, 0, 0, 0)));

    REQUIRE(g.beginDrag(GizmoHandle::Uniform, down(0.2f, 0.0f), {0, 0, -1}));
    d = g.drag(down(0.4f, 0.0f), false);
    CHECK(near(d.scale, glm::vec3(2.0f), 1e-3f));
    CHECK(near(g.drag(down(0.1f, 0.0f), false).scale, glm::vec3(0.5f), 1e-3f));
}

TEST_CASE("glissé : refus quand le rayon est parallèle à l'axe ou au plan") {
    Gizmo g = makeGizmo(GizmoMode::Translate);
    CHECK_FALSE(
        g.beginDrag(GizmoHandle::AxisX, Ray::make({0, 1, 0}, {1, 0, 0}), {0, 0, -1}));  // parallèle à X
    CHECK_FALSE(g.isDragging());
    CHECK_FALSE(g.beginDrag(GizmoHandle::PlaneXY, Ray::make({0, 1, 0.4f}, {1, 0, 0}), {0, 0, -1}));
    CHECK_FALSE(g.beginDrag(GizmoHandle::None, down(0, 0), {0, 0, -1}));
}

TEST_CASE("gizmo : maillage valide, survol en surbrillance, taille proportionnelle") {
    for (const GizmoMode mode : {GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale}) {
        Gizmo g = makeGizmo(mode);
        const GizmoMesh m = g.buildMesh();
        REQUIRE_FALSE(m.positions.empty());
        CHECK(m.positions.size() == m.colors.size());
        CHECK(m.indices.size() % 3 == 0);
        CHECK(*std::max_element(m.indices.begin(), m.indices.end()) < m.positions.size());
        float extent = 0.0f;
        for (const glm::vec3& p : m.positions) extent = std::max(extent, glm::length(p));
        CHECK(extent <= 1.2f);  // reste dans l'enveloppe annoncée (size = 1)

        g.setFrame({0, 0, 0}, glm::quat(1, 0, 0, 0), 3.0f);
        float bigExtent = 0.0f;
        for (const glm::vec3& p : g.buildMesh().positions) bigExtent = std::max(bigExtent, glm::length(p));
        CHECK(bigExtent == doctest::Approx(3.0f * extent).epsilon(1e-3));
    }
    Gizmo g = makeGizmo(GizmoMode::Translate);
    const auto yellow = [](const GizmoMesh& m) {
        return std::any_of(m.colors.begin(), m.colors.end(),
                           [](const glm::vec3& c) { return c.r > 0.95f && c.g > 0.8f; });
    };
    CHECK_FALSE(yellow(g.buildMesh()));
    g.setHover(GizmoHandle::AxisX);
    CHECK(yellow(g.buildMesh()));
}
