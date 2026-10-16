// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/math/quat_utils.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::Rng;

TEST_CASE("fromTo : vecteurs aléatoires") {
    Rng rng;
    for (int i = 0; i < 1000; ++i) {
        glm::vec3 a = rng.unit() * rng.uniform(0.1f, 5.0f), b = rng.unit() * rng.uniform(0.1f, 5.0f);
        glm::quat q = fromTo(a, b);
        CHECK(testutil::near(q * glm::normalize(a), glm::normalize(b), 1e-4f));
        CHECK(std::abs(glm::length(q) - 1.0f) < 1e-5f);
    }
}

TEST_CASE("fromTo : vecteurs parallèles -> identité") {
    CHECK(testutil::sameRot(fromTo({1, 0, 0}, {3, 0, 0}), glm::quat(1, 0, 0, 0)));
    glm::quat q = fromTo({0, 2, 0}, {0, 2, 0});
    CHECK(testutil::sameRot(q, glm::quat(1, 0, 0, 0)));
}

TEST_CASE("fromTo : vecteurs opposés -> demi-tour valide") {
    for (glm::vec3 a : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), glm::vec3(-1, 0, 0),
                        glm::normalize(glm::vec3(1, 2, 3))}) {
        glm::quat q = fromTo(a, -a);
        CHECK(testutil::near(q * a, -a, 1e-4f));
        CHECK(std::abs(glm::length(q) - 1.0f) < 1e-5f);
        CHECK_FALSE(std::isnan(q.w));
    }
}

TEST_CASE("fromTo : vecteur nul -> identité, pas de NaN") {
    glm::quat q = fromTo({0, 0, 0}, {1, 0, 0});
    CHECK(testutil::sameRot(q, glm::quat(1, 0, 0, 0)));
    CHECK_FALSE(std::isnan(q.w));
}

TEST_CASE("slerpShortest : extrémités") {
    Rng rng;
    for (int i = 0; i < 100; ++i) {
        glm::quat a = rng.quat(), b = rng.quat();
        CHECK(testutil::sameRot(slerpShortest(a, b, 0.0f), a));
        CHECK(testutil::sameRot(slerpShortest(a, b, 1.0f), b));
    }
}

TEST_CASE("slerpShortest : chemin le plus court à travers 180°") {
    // De 0° à 350° autour de Z : le milieu doit être -5° (et non 175°).
    glm::quat a(1, 0, 0, 0);
    glm::quat b = glm::angleAxis(glm::radians(350.0f), glm::vec3(0, 0, 1));
    glm::quat mid = slerpShortest(a, b, 0.5f);
    CHECK(testutil::sameRot(mid, glm::angleAxis(glm::radians(-5.0f), glm::vec3(0, 0, 1))));
}

TEST_CASE("slerpShortest : angle proportionnel à u") {
    glm::quat a(1, 0, 0, 0);
    glm::quat b = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    CHECK(testutil::sameRot(slerpShortest(a, b, 0.5f),
                            glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0))));
}

TEST_CASE("swingTwist : q = swing * twist, twist autour de l'axe, swing perpendiculaire") {
    Rng rng;
    for (int i = 0; i < 500; ++i) {
        glm::quat q = rng.quat();
        glm::vec3 axis = rng.unit();
        SwingTwist st = swingTwist(q, axis);
        CHECK(testutil::sameRot(st.swing * st.twist, q, 1e-4f));
        // le twist n'a pas de composante hors de l'axe
        glm::vec3 tv(st.twist.x, st.twist.y, st.twist.z);
        CHECK(glm::length(tv - axis * glm::dot(tv, axis)) < 1e-4f);
        // l'axe du swing est perpendiculaire à l'axe de torsion
        glm::vec3 sv(st.swing.x, st.swing.y, st.swing.z);
        CHECK(std::abs(glm::dot(sv, axis)) < 1e-4f);
    }
}

TEST_CASE("swingTwist : rotation pure autour de l'axe / pure perpendiculaire") {
    glm::vec3 axis(0, 1, 0);
    SwingTwist a = swingTwist(glm::angleAxis(0.8f, axis), axis);
    CHECK(testutil::sameRot(a.swing, glm::quat(1, 0, 0, 0), 1e-4f));
    CHECK(testutil::sameRot(a.twist, glm::angleAxis(0.8f, axis), 1e-4f));

    SwingTwist b = swingTwist(glm::angleAxis(0.8f, glm::vec3(1, 0, 0)), axis);
    CHECK(testutil::sameRot(b.twist, glm::quat(1, 0, 0, 0), 1e-4f));
    CHECK(testutil::sameRot(b.swing, glm::angleAxis(0.8f, glm::vec3(1, 0, 0)), 1e-4f));
}

TEST_CASE("swingTwist : demi-tour perpendiculaire (w = 0) sans NaN") {
    SwingTwist st = swingTwist(glm::angleAxis(glm::pi<float>(), glm::vec3(1, 0, 0)), glm::vec3(0, 1, 0));
    CHECK_FALSE(std::isnan(st.swing.w));
    CHECK_FALSE(std::isnan(st.twist.w));
}

TEST_CASE("Euler XYZ : convention extrinsèque Rz*Ry*Rx") {
    // x = 90° puis z = 90° : Y -> Z (par Rx) puis Z reste Z (par Rz). L'ordre inverse donnerait -X.
    glm::quat q = fromEulerXYZ({glm::half_pi<float>(), 0, glm::half_pi<float>()});
    CHECK(testutil::near(q * glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), 1e-4f));
}

TEST_CASE("Euler XYZ : aller-retour sur des angles aléatoires") {
    Rng rng;
    for (int i = 0; i < 500; ++i) {
        glm::vec3 e(rng.uniform(-3.0f, 3.0f), rng.uniform(-1.5f, 1.5f), rng.uniform(-3.0f, 3.0f));
        glm::vec3 back = toEulerXYZ(fromEulerXYZ(e));
        CHECK(testutil::near(back, e, 2e-3f));
    }
}

TEST_CASE("Euler XYZ : blocage de cardan (y = ±90°) conserve la rotation") {
    for (float y : {glm::half_pi<float>(), -glm::half_pi<float>()}) {
        for (float x : {0.0f, 0.5f, -1.2f}) {
            glm::quat q = fromEulerXYZ({x, y, 0.4f});
            glm::vec3 e = toEulerXYZ(q);
            CHECK_FALSE(std::isnan(e.x));
            CHECK(testutil::sameRot(fromEulerXYZ(e), q, 1e-4f));
        }
    }
}

TEST_CASE("sameRotation : q et -q équivalents") {
    glm::quat q = glm::angleAxis(1.0f, glm::vec3(0, 0, 1));
    CHECK(sameRotation(q, -q));
    CHECK_FALSE(sameRotation(q, glm::angleAxis(1.5f, glm::vec3(0, 0, 1))));
}
