// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <cmath>

#include "core/skinning/paint.hpp"
#include "core/skinning/weights.hpp"
#include "test_helpers.hpp"

using namespace rigger;

namespace {

// Grille 11 x 21 sommets dans le plan XY (x in [-1, 1], y in [0, 2]), toute skinnée sur l'os `a`.
struct Grid {
    Mesh mesh;
    Skeleton sk;
    BoneId a = kNoBone, b = kNoBone, c = kNoBone;
    std::vector<glm::vec3> positions;
    Grid() {
        a = sk.addBone("a", kNoBone).value();
        Transform up;
        up.t = {0, 1, 0};
        b = sk.addBone("b", a, up).value();
        c = sk.addBone("c", b, up).value();
        constexpr int nx = 11, ny = 21;
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                Vertex v;
                v.position = {-1.0f + 2.0f * static_cast<float>(i) / (nx - 1),
                              2.0f * static_cast<float>(j) / (ny - 1), 0.0f};
                v.normal = {0, 0, 1};
                mesh.vertices.push_back(v);
                positions.push_back(v.position);
                SkinWeight s;
                s.joints[0] = a;
                s.weights[0] = 1.0f;
                mesh.skinWeights.push_back(s);
            }
        }
        for (int j = 0; j + 1 < ny; ++j) {
            for (int i = 0; i + 1 < nx; ++i) {
                const std::uint32_t p = static_cast<std::uint32_t>(j * nx + i);
                mesh.indices.insert(mesh.indices.end(), {p, p + 1, p + nx, p + 1, p + nx + 1, p + nx});
            }
        }
    }
    std::uint32_t vertexAt(float x, float y) const {
        std::uint32_t best = 0;
        for (std::uint32_t v = 0; v < positions.size(); ++v) {
            if (glm::length(positions[v] - glm::vec3(x, y, 0)) <
                glm::length(positions[best] - glm::vec3(x, y, 0)))
                best = v;
        }
        return best;
    }
};

float sum(const SkinWeight& s) {
    float t = 0;
    for (const float w : s.weights) t += w;
    return t;
}

bool valid(const SkinWeight& s, float expectedSum = -1.0f) {
    int count = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        if (!std::isfinite(s.weights[k]) || s.weights[k] < 0.0f || s.weights[k] > 1.0f + 1e-6f) return false;
        if (s.weights[k] > 0.0f) {
            ++count;
            if (s.joints[k] == kNoBone) return false;
        }
    }
    return count <= 4 && (expectedSum < 0.0f || std::abs(sum(s) - expectedSum) < 1e-4f);
}

}  // namespace

TEST_CASE("falloff : bornes et monotonie") {
    for (const Falloff f : {Falloff::Smooth, Falloff::Linear, Falloff::Constant}) {
        CHECK(falloffWeight(f, 2.0f, 1.0f) == 0.0f);
        CHECK(falloffWeight(f, 0.0f, 1.0f) == doctest::Approx(1.0f));
        float prev = 1.0f;
        for (float d = 0.0f; d <= 1.0f; d += 0.05f) {
            const float w = falloffWeight(f, d, 1.0f);
            CHECK(w <= prev + 1e-6f);
            CHECK(w >= 0.0f);
            prev = w;
        }
    }
    CHECK(falloffWeight(Falloff::Linear, 0.5f, 1.0f) == doctest::Approx(0.5f));
    CHECK(falloffWeight(Falloff::Constant, 0.99f, 1.0f) == 1.0f);
}

TEST_CASE("setBoneWeight : la somme reste 1, <= 4 influences, valeurs valides (aléatoire)") {
    testutil::Rng rng(3);
    WeightLocks none;
    for (int trial = 0; trial < 3000; ++trial) {
        SkinWeight s;
        s.joints[0] = 0;
        s.weights[0] = 1.0f;
        for (int step = 0; step < 12; ++step) {
            const BoneId bone = static_cast<BoneId>(rng.integer(0, 7));
            setBoneWeight(s, bone, rng.uniform(-0.5f, 1.5f), none, true);
            REQUIRE(valid(s, 1.0f));
        }
    }
}

TEST_CASE("setBoneWeight : os verrouillés jamais modifiés, somme conservée") {
    testutil::Rng rng(8);
    for (int trial = 0; trial < 2000; ++trial) {
        SkinWeight s;
        s.joints = {0, 1, 2, kNoBone};
        s.weights = {0.5f, 0.3f, 0.2f, 0.0f};
        WeightLocks locks;
        locks.set(1, true);
        for (int step = 0; step < 8; ++step) {
            const float lockedBefore = influenceOf(s, 1);
            const BoneId bone = static_cast<BoneId>(rng.integer(0, 5));
            setBoneWeight(s, bone, rng.uniform(0.0f, 1.0f), locks, true);
            CHECK(influenceOf(s, 1) == lockedBefore);
            REQUIRE(valid(s, 1.0f));
        }
    }
    // Un os verrouillé refuse d'être peint.
    SkinWeight s;
    s.joints[0] = 0;
    s.weights[0] = 1.0f;
    WeightLocks locks;
    locks.set(0, true);
    CHECK_FALSE(setBoneWeight(s, 0, 0.2f, locks, true));
    CHECK(influenceOf(s, 0) == 1.0f);
    // Toute la masse est verrouillée : le nouvel os ne peut rien prendre.
    setBoneWeight(s, 1, 1.0f, locks, true);
    CHECK(influenceOf(s, 0) == 1.0f);
    CHECK(influenceOf(s, 1) == 0.0f);
}

TEST_CASE("setBoneWeight : 5e os éjecte le plus petit non verrouillé") {
    SkinWeight s;
    s.joints = {0, 1, 2, 3};
    s.weights = {0.4f, 0.3f, 0.2f, 0.1f};
    WeightLocks none;
    CHECK(setBoneWeight(s, 4, 0.5f, none, true));
    CHECK(influenceOf(s, 3) == 0.0f);
    CHECK(influenceOf(s, 4) == doctest::Approx(0.5f));
    CHECK(valid(s, 1.0f));
}

TEST_CASE("setBoneWeight sans normalisation : seul l'os visé change") {
    SkinWeight s;
    s.joints = {0, 1, kNoBone, kNoBone};
    s.weights = {0.6f, 0.4f, 0.0f, 0.0f};
    WeightLocks none;
    setBoneWeight(s, 1, 0.9f, none, false);
    CHECK(influenceOf(s, 0) == doctest::Approx(0.6f));
    CHECK(influenceOf(s, 1) == doctest::Approx(0.9f));
    CHECK(valid(s));
}

TEST_CASE("pinceau : ajouter, soustraire, remplacer") {
    Grid g;
    const MeshTopology topo = MeshTopology::build(g.mesh);
    WeightLocks none;
    Brush brush;
    brush.radius = 0.3f;
    brush.strength = 0.5f;
    brush.falloff = Falloff::Constant;
    brush.mode = BrushMode::Add;
    const auto hits = collectBrushHits(g.positions, {0, 1.0f, 0}, brush);
    REQUIRE(!hits.empty());
    const auto changed = applyBrushHits(g.mesh, topo, hits, brush, g.b, none, true);
    CHECK(changed.size() == hits.size());
    const std::uint32_t centre = g.vertexAt(0, 1.0f);
    CHECK(influenceOf(g.mesh.skinWeights[centre], g.b) == doctest::Approx(0.5f));
    CHECK(influenceOf(g.mesh.skinWeights[g.vertexAt(0, 1.8f)], g.b) == 0.0f);  // hors rayon
    for (const SkinWeight& s : g.mesh.skinWeights) CHECK(valid(s, 1.0f));

    brush.mode = BrushMode::Subtract;
    brush.strength = 0.2f;
    applyBrushHits(g.mesh, topo, hits, brush, g.b, none, true);
    CHECK(influenceOf(g.mesh.skinWeights[centre], g.b) == doctest::Approx(0.3f));
    CHECK(influenceOf(g.mesh.skinWeights[centre], g.a) == doctest::Approx(0.7f));

    brush.mode = BrushMode::Replace;
    brush.strength = 1.0f;
    brush.value = 0.25f;
    applyBrushHits(g.mesh, topo, hits, brush, g.b, none, true);
    CHECK(influenceOf(g.mesh.skinWeights[centre], g.b) == doctest::Approx(0.25f));
}

TEST_CASE("pinceau lisser : adoucit une marche de poids") {
    Grid g;
    const MeshTopology topo = MeshTopology::build(g.mesh);
    WeightLocks none;
    for (std::uint32_t v = 0; v < g.positions.size(); ++v) {  // marche : b = 1 au-dessus de y = 1
        if (g.positions[v].y > 1.0f) setBoneWeight(g.mesh.skinWeights[v], g.b, 1.0f, none, true);
    }
    const auto stepAcross = [&] {
        return influenceOf(g.mesh.skinWeights[g.vertexAt(0, 1.1f)], g.b) -
               influenceOf(g.mesh.skinWeights[g.vertexAt(0, 0.9f)], g.b);
    };
    const float before = stepAcross();
    Brush brush;
    brush.mode = BrushMode::Smooth;
    brush.radius = 0.4f;
    brush.strength = 1.0f;
    brush.falloff = Falloff::Constant;
    for (int i = 0; i < 3; ++i) {
        applyBrushHits(g.mesh, topo, collectBrushHits(g.positions, {0, 1.0f, 0}, brush), brush, g.b, none,
                       true);
    }
    CHECK(stepAcross() < before);
    for (const SkinWeight& s : g.mesh.skinWeights) CHECK(valid(s, 1.0f));
}

TEST_CASE("pinceau : os verrouillé respecté, masque de sommets respecté") {
    Grid g;
    const MeshTopology topo = MeshTopology::build(g.mesh);
    WeightLocks locks;
    locks.set(g.a, true);  // tous les poids sont sur a : rien ne peut bouger
    Brush brush;
    brush.radius = 0.5f;
    brush.strength = 1.0f;
    const auto hits = collectBrushHits(g.positions, {0, 1, 0}, brush);
    CHECK(applyBrushHits(g.mesh, topo, hits, brush, g.b, locks, true).empty());
    for (const SkinWeight& s : g.mesh.skinWeights) CHECK(influenceOf(s, g.a) == 1.0f);

    std::vector<std::uint8_t> mask(g.positions.size(), 0);
    mask[g.vertexAt(0, 1.0f)] = 1;
    const auto masked = collectBrushHits(g.positions, {0, 1, 0}, brush, &mask);
    CHECK(masked.size() == 1);
}

TEST_CASE("symétrie X : appariement de sommets et coup de pinceau miroir cohérent") {
    Grid g;
    const auto map = mirrorVertexMap(g.positions, 1e-4f);
    for (std::uint32_t v = 0; v < map.size(); ++v) {
        REQUIRE(map[v] != kNoVertex);
        CHECK(map[map[v]] == v);
        CHECK(g.positions[map[v]].x == doctest::Approx(-g.positions[v].x));
    }
    // Maillage asymétrique : un sommet sans jumeau -> kNoVertex.
    std::vector<glm::vec3> odd = {{1, 0, 0}, {-1, 0, 0}, {0.5f, 0, 0}};
    const auto m2 = mirrorVertexMap(odd, 1e-4f);
    CHECK(m2[0] == 1);
    CHECK(m2[2] == kNoVertex);

    const MeshTopology topo = MeshTopology::build(g.mesh);
    WeightLocks none;
    Brush brush;
    brush.radius = 0.4f;
    brush.strength = 0.7f;
    const auto hits = collectBrushHits(g.positions, {0.5f, 1.0f, 0}, brush);
    BrushHits mirrored;
    for (const auto& [v, f] : hits) {
        if (map[v] != kNoVertex) mirrored.emplace_back(map[v], f);
    }
    applyBrushHits(g.mesh, topo, hits, brush, g.b, none, true);
    applyBrushHits(g.mesh, topo, mirrored, brush, g.b, none, true);
    for (std::uint32_t v = 0; v < map.size(); ++v) {
        CHECK(influenceOf(g.mesh.skinWeights[v], g.b) ==
              doctest::Approx(influenceOf(g.mesh.skinWeights[map[v]], g.b)));
    }
}

TEST_CASE("visibilité : un sommet derrière un autre triangle n'est pas visible") {
    // Deux triangles parallèles : devant (z = 1) et derrière (z = 0), vus depuis z = 5.
    std::vector<glm::vec3> p = {{-1, -1, 1},       {1, -1, 1},       {0, 1, 1},
                                {-0.2f, -0.2f, 0}, {0.2f, -0.2f, 0}, {0, 0.2f, 0}};
    std::vector<glm::vec3> n(6, glm::vec3(0, 0, 1));
    std::vector<std::uint32_t> idx = {0, 1, 2, 3, 4, 5};
    const auto vis = visibleVertices(p, n, idx, {0, 0, 5}, {0, 1, 2, 3, 4, 5});
    CHECK(vis[0] == 1);
    CHECK(vis[3] == 0);
    CHECK(vis[5] == 0);
    // Dos tourné à l'œil.
    n[0] = {0, 0, -1};
    CHECK(visibleVertices(p, n, idx, {0, 0, 5}, {0})[0] == 0);
}

TEST_CASE("skinning automatique : proximité des os, somme 1, <= 4 influences") {
    Grid g;
    g.mesh.skinWeights.clear();
    autoSkin(g.mesh, g.sk);
    REQUIRE(g.mesh.skinWeights.size() == g.mesh.vertices.size());
    for (const SkinWeight& s : g.mesh.skinWeights) CHECK(valid(s, 1.0f));
    // Os a : y in [0,1], os b : y in [1,2] ; le bas est dominé par a, le haut par b.
    const SkinWeight& low = g.mesh.skinWeights[g.vertexAt(0, 0.1f)];
    const SkinWeight& high = g.mesh.skinWeights[g.vertexAt(0, 1.9f)];
    CHECK(influenceOf(low, g.a) > influenceOf(low, g.b));
    CHECK(influenceOf(high, g.b) > influenceOf(high, g.a));
    // Continuité : le poids de b croît avec la hauteur (au centre).
    float previous = -1.0f;
    for (float y = 0.0f; y <= 1.2f; y += 0.2f) {  // au-delà, l'os c (bout de b) prend le relais
        const float w = influenceOf(g.mesh.skinWeights[g.vertexAt(0, y)], g.b);
        CHECK(w >= previous - 0.05f);
        previous = w;
    }
}

TEST_CASE("skinning automatique ignore les os de contrôle") {
    Grid g;
    Transform far;
    far.t = {0, 1, 0};
    const BoneId ctl = g.sk.addBone("ctl", kNoBone, far).value();
    g.sk.setFlags(ctl, BoneFlags::Control);
    g.mesh.skinWeights.clear();
    autoSkin(g.mesh, g.sk);
    for (const SkinWeight& s : g.mesh.skinWeights) CHECK(influenceOf(s, ctl) == 0.0f);
}

TEST_CASE("outils globaux : normaliser, limiter, élaguer, copier") {
    Grid g;
    SkinWeight messy;
    messy.joints = {g.a, g.b, g.c, 9};
    messy.weights = {0.5f, 0.3f, 0.005f, 0.4f};  // somme 1.205
    g.mesh.skinWeights[0] = messy;
    CHECK(normalizeAllWeights(g.mesh) >= 1);
    CHECK(sum(g.mesh.skinWeights[0]) == doctest::Approx(1.0f));
    CHECK(limitAllInfluences(g.mesh, 2) == 1);
    int count = 0;
    for (const float w : g.mesh.skinWeights[0].weights) count += w > 0.0f;
    CHECK(count == 2);
    CHECK(valid(g.mesh.skinWeights[0], 1.0f));
    SkinWeight dust;
    dust.joints = {g.a, g.b, kNoBone, kNoBone};
    dust.weights = {0.995f, 0.005f, 0.0f, 0.0f};
    g.mesh.skinWeights[1] = dust;
    CHECK(pruneAllWeights(g.mesh, 0.01f) == 1);
    CHECK(influenceOf(g.mesh.skinWeights[1], g.b) == 0.0f);
    CHECK(influenceOf(g.mesh.skinWeights[1], g.a) == doctest::Approx(1.0f));

    Mesh target;
    for (const Vertex& v : g.mesh.vertices) {
        Vertex w = v;
        w.position += glm::vec3(0.001f, 0.0f, 0.0f);  // décalé de 1 mm : plus proche voisin = lui-même
        target.vertices.push_back(w);
    }
    CHECK(copyWeightsNearest(target, g.mesh) == target.vertices.size());
    for (std::size_t v = 0; v < target.vertices.size(); ++v) {
        CHECK(target.skinWeights[v].joints == g.mesh.skinWeights[v].joints);
    }
}

TEST_CASE("skinning par diffusion de chaleur : somme 1, poids lisses, os dominant correct") {
    Grid g;
    g.mesh.skinWeights.clear();
    AutoSkinOptions options;
    options.method = AutoSkinMethod::HeatDiffusion;
    autoSkin(g.mesh, g.sk, options);
    REQUIRE(g.mesh.skinWeights.size() == g.mesh.vertices.size());
    for (const SkinWeight& s : g.mesh.skinWeights) CHECK(valid(s, 1.0f));
    const SkinWeight& low = g.mesh.skinWeights[g.vertexAt(0, 0.1f)];
    const SkinWeight& high = g.mesh.skinWeights[g.vertexAt(0, 1.4f)];
    CHECK(influenceOf(low, g.a) > influenceOf(low, g.b));
    CHECK(influenceOf(high, g.b) > influenceOf(high, g.a));
    // Lisse : à 0,5 m de l'os, le poids de b ne fait jamais de saut brutal entre deux rangées voisines.
    float previous = influenceOf(g.mesh.skinWeights[g.vertexAt(0.5f, 0.0f)], g.b);
    for (float y = 0.1f; y <= 1.5f; y += 0.1f) {  // au-delà, l'os c prend le relais
        const float w = influenceOf(g.mesh.skinWeights[g.vertexAt(0.5f, y)], g.b);
        CHECK(std::abs(w - previous) < 0.35f);
        previous = w;
    }
}

TEST_CASE("diffusion de chaleur : respecte la topologie (deux nappes proches mais non connectées)") {
    // Deux feuillets parallèles (z = 0 et z = 0.05) sans arête entre eux ; l'os a ne touche que le premier, b
    // le second.
    Skeleton sk;
    const BoneId a = sk.addBone("a", kNoBone).value();
    Transform off;
    off.t = {0.0f, 0.0f, 0.05f};
    const BoneId b = sk.addBone("b", kNoBone, off).value();
    sk.setLength(a, 1.0f);
    sk.setLength(b, 1.0f);
    Mesh mesh;
    for (int sheet = 0; sheet < 2; ++sheet) {
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int j = 0; j < 6; ++j) {
            for (int i = 0; i < 3; ++i) {
                Vertex v;
                v.position = {0.1f * static_cast<float>(i), 0.2f * static_cast<float>(j),
                              0.05f * static_cast<float>(sheet)};
                mesh.vertices.push_back(v);
            }
        }
        for (int j = 0; j < 5; ++j) {
            for (int i = 0; i < 2; ++i) {
                const std::uint32_t p = base + static_cast<std::uint32_t>(j * 3 + i);
                mesh.indices.insert(mesh.indices.end(), {p, p + 1, p + 3, p + 1, p + 4, p + 3});
            }
        }
    }
    AutoSkinOptions options;
    options.method = AutoSkinMethod::HeatDiffusion;
    autoSkin(mesh, sk, options);
    for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
        CHECK(valid(mesh.skinWeights[v], 1.0f));
        const BoneId expected = v < 18 ? a : b;
        CHECK(influenceOf(mesh.skinWeights[v], expected) > 0.9f);
    }
}

TEST_CASE("lasso : point dans un polygone (pair-impair)") {
    const std::vector<glm::vec2> square = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    CHECK(pointInPolygon({2, 2}, square));
    CHECK_FALSE(pointInPolygon({5, 2}, square));
    CHECK_FALSE(pointInPolygon({-1, -1}, square));
    // Polygone concave en L.
    const std::vector<glm::vec2> ell = {{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4}};
    CHECK(pointInPolygon({0.5f, 3}, ell));
    CHECK(pointInPolygon({3, 0.5f}, ell));
    CHECK_FALSE(pointInPolygon({3, 3}, ell));
    CHECK_FALSE(pointInPolygon({1, 1}, std::vector<glm::vec2>{}));
}
