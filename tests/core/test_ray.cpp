// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

#include "core/math/ray.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;
using testutil::Rng;

TEST_CASE("Ray::make normalise la direction") {
    Ray r = Ray::make({0, 0, 0}, {0, 0, -10});
    CHECK(std::abs(glm::length(r.dir) - 1.0f) < 1e-6f);
}

TEST_CASE("sphère : touche, rate, derrière, dedans, tangent") {
    Ray r = Ray::make({0, 0, 5}, {0, 0, -1});
    auto hit = intersectSphere(r, {0, 0, 0}, 1.0f);
    REQUIRE(hit.has_value());
    CHECK(*hit == doctest::Approx(4.0f));

    CHECK_FALSE(intersectSphere(r, {3, 0, 0}, 1.0f).has_value());
    CHECK_FALSE(intersectSphere(Ray::make({0, 0, 5}, {0, 0, 1}), {0, 0, 0}, 1.0f).has_value());
    CHECK(*intersectSphere(Ray::make({0, 0, 0.2f}, {0, 0, -1}), {0, 0, 0}, 1.0f) == 0.0f);

    auto tangent = intersectSphere(Ray::make({1, 0, 5}, {0, 0, -1}), {0, 0, 0}, 1.0f);
    REQUIRE(tangent.has_value());
    CHECK(*tangent == doctest::Approx(5.0f));
}

TEST_CASE("capsule : corps, capuchon, parallèle, dedans, derrière") {
    const glm::vec3 a(0, 0, 0), b(0, 2, 0);
    const float rad = 0.5f;

    auto body = intersectCapsule(Ray::make({0, 1, 5}, {0, 0, -1}), a, b, rad);
    REQUIRE(body.has_value());
    CHECK(*body == doctest::Approx(4.5f));

    auto cap = intersectCapsule(Ray::make({0, 5, 0}, {0, -1, 0}), a, b, rad);  // capuchon haut à y = 2.5
    REQUIRE(cap.has_value());
    CHECK(*cap == doctest::Approx(2.5f));

    CHECK_FALSE(
        intersectCapsule(Ray::make({2, -5, 0}, {0, 1, 0}), a, b, rad).has_value());   // parallèle, à côté
    CHECK(*intersectCapsule(Ray::make({0.1f, 1, 0}, {1, 0, 0}), a, b, rad) == 0.0f);  // origine dedans
    CHECK_FALSE(intersectCapsule(Ray::make({0, 1, 5}, {0, 0, 1}), a, b, rad).has_value());  // derrière
    CHECK_FALSE(
        intersectCapsule(Ray::make({0, 4, 5}, {0, 0, -1}), a, b, rad).has_value());  // au-dessus du capuchon
}

TEST_CASE("capsule : segment dégénéré (a = b) se comporte comme une sphère") {
    auto hit = intersectCapsule(Ray::make({0, 0, 5}, {0, 0, -1}), {0, 0, 0}, {0, 0, 0}, 1.0f);
    REQUIRE(hit.has_value());
    CHECK(*hit == doctest::Approx(4.0f));
}

TEST_CASE("capsule : cohérence avec la distance au segment (rayons aléatoires)") {
    Rng rng(99);
    auto dist = [](const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
        glm::vec3 ab = b - a;
        float h = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        return glm::length(p - (a + ab * h));
    };
    int hits = 0, misses = 0;
    for (int i = 0; i < 400; ++i) {
        glm::vec3 a = rng.vec(2), b = rng.vec(2);
        if (glm::length(b - a) < 0.2f) continue;
        float rad = rng.uniform(0.2f, 1.0f);
        // On vise un point proche du segment (avec un écart aléatoire) pour obtenir touches ET ratés.
        glm::vec3 origin = rng.vec(4);
        glm::vec3 target = glm::mix(a, b, rng.uniform(0.0f, 1.0f)) + rng.vec(1.3f);
        if (glm::length(target - origin) < 0.1f) continue;
        Ray ray = Ray::make(origin, target - origin);
        auto res = intersectCapsule(ray, a, b, rad);
        if (res) {
            ++hits;
            if (*res > 0.0f) {
                CHECK(std::abs(dist(ray.at(*res), a, b) - rad) < 3e-3f);  // on est sur la surface
                for (float t = 0.0f; t < *res - 0.02f; t += 0.02f)        // et rien avant
                    CHECK(dist(ray.at(t), a, b) > rad - 3e-3f);
            } else {
                CHECK(dist(ray.origin, a, b) <= rad + 1e-4f);
            }
        } else {
            ++misses;
            for (float t = 0.0f; t < 30.0f; t += 0.01f) CHECK(dist(ray.at(t), a, b) > rad - 6e-3f);
        }
    }
    CHECK(hits > 20);
    CHECK(misses > 20);
}

TEST_CASE("triangle : touche au centre, coordonnées barycentriques") {
    glm::vec3 v0(0, 0, 0), v1(1, 0, 0), v2(0, 1, 0);
    auto hit = intersectTriangle(Ray::make({0.25f, 0.25f, 3}, {0, 0, -1}), v0, v1, v2);
    REQUIRE(hit.has_value());
    CHECK(hit->t == doctest::Approx(3.0f));
    CHECK(hit->u == doctest::Approx(0.25f));
    CHECK(hit->v == doctest::Approx(0.25f));
}

TEST_CASE("triangle : dehors, parallèle, derrière") {
    glm::vec3 v0(0, 0, 0), v1(1, 0, 0), v2(0, 1, 0);
    CHECK_FALSE(intersectTriangle(Ray::make({0.8f, 0.8f, 3}, {0, 0, -1}), v0, v1, v2).has_value());
    CHECK_FALSE(intersectTriangle(Ray::make({0.2f, 0.2f, 3}, {1, 0, 0}), v0, v1, v2).has_value());
    CHECK_FALSE(intersectTriangle(Ray::make({0.2f, 0.2f, 3}, {0, 0, 1}), v0, v1, v2).has_value());
}

TEST_CASE("triangle : culling des faces arrière") {
    glm::vec3 v0(0, 0, 0), v1(1, 0, 0), v2(0, 1, 0);  // normale +Z
    Ray front = Ray::make({0.2f, 0.2f, 3}, {0, 0, -1});
    Ray back = Ray::make({0.2f, 0.2f, -3}, {0, 0, 1});
    CHECK(intersectTriangle(front, v0, v1, v2, true).has_value());
    CHECK_FALSE(intersectTriangle(back, v0, v1, v2, true).has_value());
    CHECK(intersectTriangle(back, v0, v1, v2, false).has_value());
}

TEST_CASE("plan : intersection, parallèle, derrière") {
    const Ray r = Ray::make({0, 0, 5}, {0, 0, -1});
    CHECK(*intersectPlane(r, {0, 0, 1}, {0, 0, 2}) ==
          doctest::Approx(4.0f));                                      // normale non unitaire acceptée
    CHECK_FALSE(intersectPlane(r, {0, 0, 1}, {1, 0, 0}).has_value());  // parallèle
    CHECK_FALSE(intersectPlane(r, {0, 0, 9}, {0, 0, 1}).has_value());  // plan derrière l'origine
}

TEST_CASE("rayon / droite : point le plus proche pour glisser sur un axe") {
    // Rayon vers -Z passant par x = 2 ; axe X : le point le plus proche est x = 2, distance = hauteur du
    // rayon.
    const Ray r = Ray::make({2, 1, 5}, {0, 0, -1});
    const auto hit = closestRayToLine(r, {0, 0, 0}, {1, 0, 0});
    REQUIRE(hit.has_value());
    CHECK(hit->tLine == doctest::Approx(2.0f));
    CHECK(hit->tRay == doctest::Approx(5.0f));
    CHECK(hit->distance == doctest::Approx(1.0f));
    // Rayon parallèle à la droite : pas de solution ; droite derrière la caméra : rejetée.
    CHECK_FALSE(closestRayToLine(Ray::make({0, 1, 0}, {1, 0, 0}), {0, 0, 0}, {1, 0, 0}).has_value());
    CHECK_FALSE(closestRayToLine(Ray::make({0, 0, 5}, {0, 0, 1}), {0, 0, 0}, {1, 0, 0}).has_value());
}

TEST_CASE("rayon / droite : distance exacte et normale commune sur 1000 cas aléatoires") {
    Rng rng(7);
    for (int i = 0; i < 1000; ++i) {
        const Ray r = Ray::make(rng.vec(5.0f), rng.unit());
        const glm::vec3 p = rng.vec(5.0f);
        const glm::vec3 e = rng.unit();
        const auto hit = closestRayToLine(r, p, e);
        if (!hit) continue;
        const glm::vec3 pr = r.at(hit->tRay), pl = p + e * hit->tLine;
        CHECK(glm::length(pr - pl) == doctest::Approx(hit->distance).epsilon(1e-3));
        // La normale commune est orthogonale aux deux directions.
        CHECK(std::abs(glm::dot(pr - pl, r.dir)) < 2e-3f);
        CHECK(std::abs(glm::dot(pr - pl, e)) < 2e-3f);
    }
}

TEST_CASE("anneau : touche le cercle à la tolérance près, rate le centre et l'extérieur") {
    const glm::vec3 c(0, 0, 0), n(0, 0, 1);
    const Ray onRing = Ray::make({1, 0, 5}, {0, 0, -1});
    REQUIRE(intersectRing(onRing, c, n, 1.0f, 0.1f).has_value());
    CHECK(*intersectRing(onRing, c, n, 1.0f, 0.1f) == doctest::Approx(5.0f));
    CHECK(intersectRing(Ray::make({1.05f, 0, 5}, {0, 0, -1}), c, n, 1.0f, 0.1f)
              .has_value());  // dans la tolérance
    CHECK_FALSE(intersectRing(Ray::make({1.2f, 0, 5}, {0, 0, -1}), c, n, 1.0f, 0.1f).has_value());  // dehors
    CHECK_FALSE(intersectRing(Ray::make({0, 0, 5}, {0, 0, -1}), c, n, 1.0f, 0.1f).has_value());     // centre
    CHECK_FALSE(intersectRing(Ray::make({1, 0, 5}, {1, 0, 0}), c, n, 1.0f, 0.1f).has_value());  // parallèle
}

TEST_CASE("screenRay : le centre de l'écran vise la cible, les coins s'écartent, ortho = rayons parallèles") {
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 persp = glm::perspective(glm::radians(60.0f), 2.0f, 0.1f, 100.0f);
    const Ray center = screenRay(persp * view, 400.0f, 200.0f, 800.0f, 400.0f);
    CHECK(near(center.dir, {0, 0, -1}, 1e-4f));
    CHECK(glm::length(glm::cross(center.dir, glm::vec3(0.0f) - center.origin)) <
          1e-3f);  // passe par la cible

    // Pixel en haut à droite : le rayon monte (y > 0) et va vers la droite (x > 0).
    const Ray corner = screenRay(persp * view, 800.0f, 0.0f, 800.0f, 400.0f);
    CHECK(corner.dir.x > 0.0f);
    CHECK(corner.dir.y > 0.0f);
    // Un point projeté puis déprojeté est sur le rayon (aller-retour).
    const glm::vec3 p(1.0f, 0.5f, -1.0f);
    const glm::vec4 clip = persp * view * glm::vec4(p, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const Ray through = screenRay(persp * view, (ndc.x * 0.5f + 0.5f) * 800.0f,
                                  (0.5f - ndc.y * 0.5f) * 400.0f, 800.0f, 400.0f);
    CHECK(glm::length(glm::cross(through.dir, p - through.origin)) < 1e-3f);

    const glm::mat4 ortho = glm::ortho(-4.0f, 4.0f, -2.0f, 2.0f, 0.1f, 100.0f);
    const Ray a = screenRay(ortho * view, 100.0f, 50.0f, 800.0f, 400.0f);
    const Ray b = screenRay(ortho * view, 700.0f, 350.0f, 800.0f, 400.0f);
    CHECK(near(a.dir, b.dir, 1e-4f));                // rayons parallèles
    CHECK(glm::length(a.origin - b.origin) > 1.0f);  // mais décalés
}
