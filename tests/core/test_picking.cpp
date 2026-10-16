// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <algorithm>

#include "core/skeleton/fk.hpp"
#include "core/skeleton/picking.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;

namespace {

// A (0,0,0) -> B (0,1,0) -> C (0,2,0), D enfant de A en (1,0,0).
struct Chain {
    Skeleton sk;
    BoneId a, b, c, d;
    Chain() {
        Transform up;
        up.t = {0, 1, 0};
        Transform side;
        side.t = {1, 0, 0};
        a = sk.addBone("A", kNoBone).value();
        b = sk.addBone("B", a, up).value();
        c = sk.addBone("C", b, up).value();
        d = sk.addBone("D", a, side).value();
    }
    std::vector<glm::mat4> globals() const { return computeGlobals(sk, sk.bindPose()); }
};

PickParams params(const glm::vec3& camera) {
    PickParams p;
    p.cameraPosition = camera;
    p.anglePerPixel = 0.001f;
    p.tolerancePixels = 8.0f;
    return p;
}

}  // namespace

TEST_CASE("boneSegments : lien parent->os et tronçon de feuille") {
    Chain ch;
    const auto g = ch.globals();
    const BoneSegments root = boneSegments(ch.sk, g, ch.a);
    CHECK_FALSE(root.hasLink);  // racine
    CHECK_FALSE(root.hasStub);  // a des enfants
    const BoneSegments mid = boneSegments(ch.sk, g, ch.b);
    CHECK(mid.hasLink);
    CHECK(near(mid.linkFrom, {0, 0, 0}));
    CHECK(near(mid.linkTo, {0, 1, 0}));
    const BoneSegments leaf = boneSegments(ch.sk, g, ch.c);
    CHECK(leaf.hasStub);
    CHECK(near(leaf.stubTo, {0, 2.1f, 0}));  // longueur par défaut 0,1 m le long de Y
}

TEST_CASE("picking : clic sur un lien, sur une articulation, dans le vide") {
    Chain ch;
    const auto g = ch.globals();
    const glm::vec3 cam(0, 1, 10);
    const auto pickAll = [](BoneId) { return true; };

    // Rayon vers -Z à travers le milieu du lien A->B : touche B.
    auto hits =
        pickBones(ch.sk, g, Ray::make(cam + glm::vec3(0, -0.5f, 0), {0, 0, -1}), params(cam), pickAll);
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().id == ch.b);

    // Au-dessus de tout : rien.
    CHECK(pickBones(ch.sk, g, Ray::make({0, 5, 10}, {0, 0, -1}), params(cam), pickAll).empty());

    // Sur l'origine de la racine (articulation sans lien) : on touche A.
    hits = pickBones(ch.sk, g, Ray::make({0, 0, 10}, {0, 0, -1}), params(cam), pickAll);
    REQUIRE_FALSE(hits.empty());
    CHECK(std::any_of(hits.begin(), hits.end(), [&](const PickHit& h) { return h.id == ch.a; }));
}

TEST_CASE("picking : la tolérance grandit avec la distance (taille constante à l'écran)") {
    Chain ch;
    const auto g = ch.globals();
    const auto pickAll = [](BoneId) { return true; };
    // Rayon décalé de 0,05 m du lien A->B, depuis 10 m puis 100 m : rayon monde = 8 px * 0,001 * distance.
    const auto missesAt = [&](float distance) {
        const glm::vec3 cam(0.05f, 0.5f, distance);
        return pickBones(ch.sk, g, Ray::make(cam, {0, 0, -1}), params(cam), pickAll).empty();
    };
    CHECK(missesAt(2.0f));         // tolérance 0,016 m : raté
    CHECK_FALSE(missesAt(20.0f));  // tolérance 0,16 m : touché
}

TEST_CASE("picking : os superposés triés par distance, clic répété = cycle") {
    Chain ch;
    const auto g = ch.globals();
    const glm::vec3 cam(0, 0.5f, 10);
    // Rayon le long de Y passant dans l'axe de la chaîne : touche A (articulation), B et C par leurs liens.
    const Ray ray = Ray::make({0, 5, 0}, {0, -1, 0});
    const auto hits = pickBones(ch.sk, g, ray, params(cam), [](BoneId) { return true; });
    REQUIRE(hits.size() >= 3);
    CHECK(std::is_sorted(hits.begin(), hits.end(),
                         [](const PickHit& x, const PickHit& y) { return x.t < y.t; }));

    BoneId selected = kNoBone;
    std::vector<BoneId> visited;
    for (std::size_t i = 0; i < hits.size() + 1; ++i) {
        selected = cyclePick(hits, selected);
        visited.push_back(selected);
    }
    CHECK(visited.front() == hits.front().id);  // premier clic : le plus proche
    CHECK(visited[1] == hits[1].id);            // clic suivant : le suivant
    CHECK(visited.back() == hits.front().id);   // retour au début après un tour complet
    CHECK(cyclePick({}, ch.a) == kNoBone);
    CHECK(cyclePick(hits, ch.d) == hits.front().id);  // sélection actuelle hors liste : on repart du début
}

TEST_CASE("picking : les os non sélectionnables (masqués, verrouillés, hors isolation) sont ignorés") {
    Chain ch;
    const auto g = ch.globals();
    const glm::vec3 cam(0, 1, 10);
    const Ray ray = Ray::make(cam + glm::vec3(0, -0.5f, 0), {0, 0, -1});  // touche B
    CHECK(pickBones(ch.sk, g, ray, params(cam), [](BoneId) { return true; }).front().id == ch.b);
    const auto hits = pickBones(ch.sk, g, ray, params(cam), [&](BoneId id) { return id != ch.b; });
    CHECK(std::none_of(hits.begin(), hits.end(), [&](const PickHit& h) { return h.id == ch.b; }));
}

TEST_CASE("rectangle : os dont l'origine est dans le cadre, derrière la caméra exclus") {
    Chain ch;
    const auto g = ch.globals();
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 1, 6), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 50.0f);
    const glm::mat4 vp = proj * view;

    const auto all = bonesInRect(ch.sk, g, vp, {-1, -1}, {1, 1}, [](BoneId) { return true; });
    CHECK(all.size() == 4);
    // Moitié gauche de l'écran : seuls les os à x <= 0 (A, B, C ; D est à x = 1 donc à droite).
    const auto left = bonesInRect(ch.sk, g, vp, {-1, -1}, {0.01f, 1}, [](BoneId) { return true; });
    CHECK(std::find(left.begin(), left.end(), ch.d) == left.end());
    CHECK(std::find(left.begin(), left.end(), ch.c) != left.end());
    // Coins donnés dans le désordre : même résultat.
    CHECK(bonesInRect(ch.sk, g, vp, {1, 1}, {-1, -1}, [](BoneId) { return true; }).size() == 4);
    // Caméra qui regarde dans l'autre sens : rien n'est devant.
    const glm::mat4 behind = proj * glm::lookAt(glm::vec3(0, 1, 6), glm::vec3(0, 1, 12), glm::vec3(0, 1, 0));
    CHECK(bonesInRect(ch.sk, g, behind, {-1, -1}, {1, 1}, [](BoneId) { return true; }).empty());
    // Filtre : l'os C est exclu.
    const auto filtered = bonesInRect(ch.sk, g, vp, {-1, -1}, {1, 1}, [&](BoneId id) { return id != ch.c; });
    CHECK(filtered.size() == 3);
}
