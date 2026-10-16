// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/math/quat_utils.hpp"
#include "core/math/transform.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::Rng;

namespace {
Transform randomTransform(Rng& rng, bool uniformScale) {
    Transform t;
    t.t = rng.vec(5);
    t.r = rng.quat();
    t.s = uniformScale ? glm::vec3(rng.uniform(0.5f, 3.0f))
                       : glm::vec3(rng.uniform(0.5f, 3.0f), rng.uniform(0.5f, 3.0f), rng.uniform(0.5f, 3.0f));
    return t;
}
}  // namespace

TEST_CASE("Transform par défaut = identité") {
    Transform t;
    CHECK_VEC3(t.apply({1, 2, 3}), glm::vec3(1, 2, 3));
    CHECK(testutil::near(t.toMat4(), glm::mat4(1.0f)));
}

TEST_CASE("toMat4 = T * R * S") {
    Rng rng;
    for (int i = 0; i < 100; ++i) {
        Transform t = randomTransform(rng, false);
        glm::mat4 expected =
            glm::translate(glm::mat4(1.0f), t.t) * glm::mat4_cast(t.r) * glm::scale(glm::mat4(1.0f), t.s);
        CHECK(testutil::near(t.toMat4(), expected));
    }
}

TEST_CASE("apply est cohérent avec la matrice") {
    Rng rng;
    for (int i = 0; i < 100; ++i) {
        Transform t = randomTransform(rng, false);
        glm::vec3 p = rng.vec(3);
        CHECK(testutil::near(t.apply(p), glm::vec3(t.toMat4() * glm::vec4(p, 1.0f))));
    }
}

TEST_CASE("applyDir ignore la translation") {
    Transform t;
    t.t = {10, 20, 30};
    t.r = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    CHECK_VEC3(t.applyDir({1, 0, 0}), glm::vec3(0, 1, 0));
}

TEST_CASE("fromMat4(toMat4(x)) redonne x (échelle non uniforme positive)") {
    Rng rng;
    for (int i = 0; i < 200; ++i) {
        Transform t = randomTransform(rng, false);
        Transform u = Transform::fromMat4(t.toMat4());
        CHECK(testutil::near(u.t, t.t));
        CHECK(testutil::near(u.s, t.s));
        CHECK(testutil::sameRot(u.r, t.r));
    }
}

TEST_CASE("fromMat4 gère les miroirs (déterminant négatif)") {
    Transform t;
    t.t = {1, 2, 3};
    t.r = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1, 1, 0)));
    t.s = {-2, 1, 3};
    glm::mat4 m = t.toMat4();
    CHECK(glm::determinant(glm::mat3(m)) < 0.0f);
    CHECK(testutil::near(Transform::fromMat4(m).toMat4(), m));
}

TEST_CASE("T * T^-1 = identité (échelle uniforme)") {
    Rng rng;
    for (int i = 0; i < 500; ++i) {
        Transform t = randomTransform(rng, true);
        CHECK(testutil::near((t * t.inverse()).toMat4(), glm::mat4(1.0f)));
        CHECK(testutil::near((t.inverse() * t).toMat4(), glm::mat4(1.0f)));
    }
}

TEST_CASE("inverse() est l'inverse de la matrice (échelle uniforme)") {
    Rng rng;
    for (int i = 0; i < 100; ++i) {
        Transform t = randomTransform(rng, true);
        CHECK(testutil::near(t.inverse().toMat4(), glm::inverse(t.toMat4()), 1e-3f));
    }
}

TEST_CASE("composition = produit de matrices (échelle uniforme)") {
    Rng rng;
    for (int i = 0; i < 300; ++i) {
        Transform a = randomTransform(rng, true), b = randomTransform(rng, true);
        CHECK(testutil::near((a * b).toMat4(), a.toMat4() * b.toMat4(), 1e-3f));
    }
}

TEST_CASE("composition exacte : parent à échelle non uniforme, enfant sans rotation") {
    Rng rng;
    for (int i = 0; i < 100; ++i) {
        Transform parent = randomTransform(rng, false);  // rotation + échelle non uniforme
        Transform child;
        child.t = rng.vec(3);
        child.s = {rng.uniform(0.5f, 2.0f), rng.uniform(0.5f, 2.0f), rng.uniform(0.5f, 2.0f)};
        CHECK(testutil::near((parent * child).toMat4(), parent.toMat4() * child.toMat4(), 1e-3f));
    }
}

TEST_CASE("limite documentée : parent non uniforme + enfant tourné = cisaillement non représentable en TRS") {
    Transform parent;
    parent.s = {3, 1, 1};
    Transform child;
    child.r = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0, 0, 1));
    // La matrice exacte contient du cisaillement : le TRS ne peut pas la reproduire -> c'est pourquoi
    // computeGlobals() travaille en mat4.
    CHECK_FALSE(testutil::near((parent * child).toMat4(), parent.toMat4() * child.toMat4(), 1e-3f));
}

TEST_CASE("lerp : extrémités et milieu") {
    Transform a, b;
    a.t = {0, 0, 0};
    b.t = {2, 4, 6};
    a.s = {1, 1, 1};
    b.s = {3, 3, 3};
    CHECK_VEC3(lerp(a, b, 0.0f).t, a.t);
    CHECK_VEC3(lerp(a, b, 1.0f).t, b.t);
    CHECK_VEC3(lerp(a, b, 0.5f).t, glm::vec3(1, 2, 3));
    CHECK_VEC3(lerp(a, b, 0.5f).s, glm::vec3(2, 2, 2));
}

TEST_CASE("lerp prend le chemin le plus court (q et -q)") {
    glm::quat q = glm::angleAxis(0.6f, glm::vec3(0, 1, 0));
    Transform a, b;
    a.r = q;
    b.r = -q;  // même rotation, hémisphère opposé
    Transform m = lerp(a, b, 0.5f);
    CHECK(testutil::sameRot(m.r, q));
    CHECK(std::abs(glm::length(m.r) - 1.0f) < 1e-5f);
}
