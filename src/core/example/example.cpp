// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/example/example.hpp"

#include <cmath>

#include "core/deform/autorig.hpp"
#include "core/skinning/paint.hpp"

namespace rigger {

namespace {

struct Group {
    std::vector<std::uint32_t> indices;
    glm::vec3 color;
    const char* name;
};

// Prisme à `sides` faces entre a et b (bouchons inclus), ajouté au maillage ; indices rangés dans `group`.
void addPrism(Mesh& mesh, Group& group, const glm::vec3& a, const glm::vec3& b, float radiusA, float radiusB,
              int sides = 8) {
    const glm::vec3 axis = glm::normalize(b - a);
    const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(axis, helper));
    const glm::vec3 v = glm::cross(axis, u);
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (int ring = 0; ring < 2; ++ring) {
        const glm::vec3 center = ring == 0 ? a : b;
        const float radius = ring == 0 ? radiusA : radiusB;
        for (int i = 0; i < sides; ++i) {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(sides);
            Vertex vertex;
            vertex.position = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            mesh.vertices.push_back(vertex);
        }
    }
    const std::uint32_t capA = static_cast<std::uint32_t>(mesh.vertices.size());
    Vertex centerA, centerB;
    centerA.position = a;
    centerB.position = b;
    mesh.vertices.push_back(centerA);
    mesh.vertices.push_back(centerB);
    for (int i = 0; i < sides; ++i) {
        const std::uint32_t i0 = base + static_cast<std::uint32_t>(i),
                            i1 = base + static_cast<std::uint32_t>((i + 1) % sides);
        const std::uint32_t j0 = i0 + static_cast<std::uint32_t>(sides),
                            j1 = i1 + static_cast<std::uint32_t>(sides);
        group.indices.insert(group.indices.end(), {i0, i1, j1, i0, j1, j0});  // flanc
        group.indices.insert(group.indices.end(), {capA, i1, i0});            // bouchon a
        group.indices.insert(group.indices.end(), {capA + 1, j0, j1});        // bouchon b
    }
}

Keyframe<glm::quat> key(double time, const glm::quat& value) {
    Keyframe<glm::quat> k;
    k.time = time;
    k.value = value;
    k.mode = Interpolation::Linear;
    return k;
}

AnimationClip makeClip(const char* name) {
    AnimationClip clip;
    clip.name = name;
    clip.fps = 30.0f;
    clip.duration = 1.0;  // 30 images
    clip.loop = true;
    return clip;
}

}  // namespace

Character buildExampleCharacter() {
    Character c;
    c.name = "Rigby";
    // Repères : T-pose, 1,8 m, face à +Z.
    const Landmarks l = guessLandmarks({{-0.9f, 0.0f, -0.1f}, {0.9f, 1.8f, 0.1f}});
    HumanoidBones ids;
    c.skeleton = buildHumanoidSkeleton(l, &ids);
    const auto p = [&](Landmark m) { return l[static_cast<std::size_t>(m)]; };

    Mesh mesh;
    Group skin{{}, {0.93f, 0.74f, 0.62f}, "skin"};
    Group shirt{{}, {0.20f, 0.45f, 0.85f}, "shirt"};
    Group pants{{}, {0.22f, 0.24f, 0.30f}, "pants"};
    addPrism(mesh, shirt, p(Landmark::Pelvis), p(Landmark::Spine), 0.15f, 0.15f);
    addPrism(mesh, shirt, p(Landmark::Spine), p(Landmark::Chest), 0.15f, 0.17f);
    addPrism(mesh, shirt, p(Landmark::Chest), p(Landmark::Neck), 0.17f, 0.12f);
    addPrism(mesh, skin, p(Landmark::Neck), p(Landmark::Head), 0.05f, 0.05f);
    addPrism(mesh, skin, p(Landmark::Head), p(Landmark::HeadTop), 0.11f, 0.10f, 10);
    const std::array<std::array<Landmark, 4>, 2> arms = {
        {{Landmark::ShoulderL, Landmark::ElbowL, Landmark::WristL, Landmark::HandTipL},
         {Landmark::ShoulderR, Landmark::ElbowR, Landmark::WristR, Landmark::HandTipR}}};
    for (const auto& a : arms) {
        addPrism(mesh, shirt, p(a[0]), p(a[1]), 0.05f, 0.045f);
        addPrism(mesh, shirt, p(a[1]), p(a[2]), 0.045f, 0.04f);
        addPrism(mesh, skin, p(a[2]), p(a[3]), 0.038f, 0.03f);
    }
    const std::array<std::array<Landmark, 4>, 2> legs = {
        {{Landmark::HipL, Landmark::KneeL, Landmark::AnkleL, Landmark::ToeL},
         {Landmark::HipR, Landmark::KneeR, Landmark::AnkleR, Landmark::ToeR}}};
    for (const auto& g : legs) {
        addPrism(mesh, pants, p(g[0]), p(g[1]), 0.075f, 0.06f);
        addPrism(mesh, pants, p(g[1]), p(g[2]), 0.06f, 0.045f);
        addPrism(mesh, skin, p(g[2]), p(g[3]), 0.045f, 0.04f);
    }
    for (Group* g : {&skin, &shirt, &pants}) {
        Submesh sub;
        sub.name = g->name;
        sub.firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
        sub.indexCount = static_cast<std::uint32_t>(g->indices.size());
        sub.material.name = g->name;
        sub.material.baseColor = g->color;
        mesh.indices.insert(mesh.indices.end(), g->indices.begin(), g->indices.end());
        mesh.submeshes.push_back(sub);
    }
    mesh.recomputeNormals();
    autoSkin(mesh, c.skeleton);
    c.meshes.push_back(std::move(mesh));

    // « Wave » : le bras gauche monte (épaule) et l'avant-bras balance ; le reste reste au repos.
    AnimationClip wave = makeClip("Wave");
    const BoneId upper = ids.upperArm[0], fore = ids.foreArm[0];
    const glm::quat upperBind = c.skeleton.bone(upper).bindLocal.r,
                    foreBind = c.skeleton.bone(fore).bindLocal.r;
    const auto about = [](float angle) { return glm::angleAxis(angle, glm::vec3(0, 0, 1)); };
    for (int i = 0; i <= 4; ++i) {  // images 0, 7,5, 15, 22,5, 30
        const double t = i * 0.25;
        const float swing = (i % 2 == 0) ? 0.45f : -0.45f;
        wave.tracks[upper].rotation.keys.push_back(key(t, upperBind * about(1.1f)));
        wave.tracks[fore].rotation.keys.push_back(key(t, foreBind * about(swing)));
    }
    c.clips.push_back(std::move(wave));

    // « Walk » : cuisses en opposition de phase, genoux qui plient, bras qui balancent (marche sur place).
    AnimationClip walk = makeClip("Walk");
    const auto swingX = [](float angle) { return glm::angleAxis(angle, glm::vec3(1, 0, 0)); };
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? 1.0f : -1.0f;
        for (int i = 0; i <= 4; ++i) {
            const double t = i * 0.25;
            const float phase = sign * std::sin(glm::two_pi<float>() * static_cast<float>(t));
            const float knee =
                std::max(0.0f, sign * std::cos(glm::two_pi<float>() * static_cast<float>(t))) * 0.6f;
            walk.tracks[ids.thigh[side]].rotation.keys.push_back(
                key(t, c.skeleton.bone(ids.thigh[side]).bindLocal.r * swingX(0.5f * phase)));
            walk.tracks[ids.shin[side]].rotation.keys.push_back(
                key(t, c.skeleton.bone(ids.shin[side]).bindLocal.r * swingX(-knee)));
        }
    }
    c.clips.push_back(std::move(walk));
    return c;
}

}  // namespace rigger
