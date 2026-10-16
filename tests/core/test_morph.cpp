// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <cmath>
#include <filesystem>

#include "core/character/edit.hpp"
#include "core/deform/morph.hpp"
#include "core/skeleton/fk.hpp"
#include "io/gltf_export.hpp"
#include "io/gltf_import.hpp"
#include "io/project.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::near;

namespace {

// Un carré (2 triangles) skinné sur un os, avec deux blend shapes pilotées par des os de poids.
struct MorphRig {
    Character character;
    BoneId root = kNoBone, smile = kNoBone, blink = kNoBone, elbow = kNoBone;
    MorphRig() {
        Skeleton& sk = character.skeleton;
        root = sk.addBone("root", kNoBone).value();
        Transform up;
        up.t = {0, 1, 0};
        elbow = sk.addBone("elbow", root, up).value();
        Mesh mesh;
        const glm::vec3 corners[4] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        for (const glm::vec3& c : corners) {
            Vertex v;
            v.position = c;
            v.normal = {0, 0, 1};
            mesh.vertices.push_back(v);
            SkinWeight w;
            w.joints[0] = root;
            w.weights[0] = 1.0f;
            mesh.skinWeights.push_back(w);
        }
        mesh.indices = {0, 1, 2, 0, 2, 3};
        Submesh sub;
        sub.indexCount = 6;
        mesh.submeshes.push_back(sub);
        const auto addTarget = [&](const char* name, std::vector<glm::vec3> deltas, float restWeight) {
            Transform bind;
            bind.t.x = restWeight;
            const BoneId bone = sk.addBone(std::string("Morph_") + name, kNoBone, bind).value();
            sk.setFlags(bone, BoneFlags::Control | BoneFlags::Morph);
            MorphTarget t;
            t.name = name;
            t.positionDeltas = std::move(deltas);
            t.weightBone = bone;
            mesh.morphTargets.push_back(std::move(t));
            return bone;
        };
        smile = addTarget("smile", {{0, 0, 0}, {0, 0, 0}, {0, 0.2f, 0}, {0, 0.2f, 0}}, 0.0f);
        blink = addTarget("blink", {{0, 0, 0.5f}, {0, 0, 0.5f}, {0, 0, 0}, {0, 0, 0}}, 0.25f);
        character.meshes.push_back(std::move(mesh));
        character.name = "morphy";
    }
    Mesh& mesh() { return character.meshes[0]; }
};

std::filesystem::path tempFile(const char* name) { return std::filesystem::temp_directory_path() / name; }

}  // namespace

TEST_CASE("blend shapes : poids 0 = forme de repos, linéarité, combinaison, normales recalculées") {
    MorphRig r;
    const Mesh& mesh = r.mesh();
    const auto same = [](const std::vector<Vertex>& a, const std::vector<Vertex>& b) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!near(a[i].position, b[i].position)) return false;
        }
        return true;
    };
    CHECK(same(applyMorphs(mesh, {0.0f, 0.0f}), mesh.vertices));
    const auto full = applyMorphs(mesh, {1.0f, 0.0f});
    CHECK(near(full[2].position, {1, 1.2f, 0}));
    CHECK(near(full[0].position, {0, 0, 0}));
    const auto half = applyMorphs(mesh, {0.5f, 0.0f});
    CHECK(near(half[3].position, {0, 1.1f, 0}));
    const auto both = applyMorphs(mesh, {1.0f, 2.0f});
    CHECK(near(both[1].position, {1, 0, 1.0f}));
    CHECK(near(both[2].position, {1, 1.2f, 0}));
    CHECK(glm::length(both[0].normal) == doctest::Approx(1.0f));
    CHECK(anyMorphActive({0.0f, 0.5f}));
    CHECK_FALSE(anyMorphActive({0.0f, 0.0f}));
}

TEST_CASE("blend shapes : le poids vient de la translation X de l'os de poids") {
    MorphRig r;
    Pose pose = r.character.skeleton.bindPose();
    auto w = morphWeights(r.mesh(), r.character.skeleton, pose);
    REQUIRE(w.size() == 2);
    CHECK(w[0] == doctest::Approx(0.0f));
    CHECK(w[1] == doctest::Approx(0.25f));  // poids de repos de la forme « blink »
    pose[r.smile].t.x = 0.8f;
    w = morphWeights(r.mesh(), r.character.skeleton, pose);
    CHECK(w[0] == doctest::Approx(0.8f));
    CHECK(morphWeights(r.mesh(), r.character.skeleton, Pose{})[1] ==
          doctest::Approx(0.25f));  // pose trop courte : repos
}

TEST_CASE("shapes correctives : pilotées par l'angle d'une articulation") {
    MorphRig r;
    MorphDriver d;
    d.bone = r.elbow;
    d.axis = {0, 0, 1};
    d.angle0 = 0.0f;
    d.angle1 = 1.0f;
    Pose pose = r.character.skeleton.bindPose();
    CHECK(driverWeight(d, r.character.skeleton, pose) == doctest::Approx(0.0f));
    const glm::quat bindRot = r.character.skeleton.bone(r.elbow).bindLocal.r;
    pose[r.elbow].r = bindRot * glm::angleAxis(0.5f, glm::vec3(0, 0, 1));
    CHECK(driverWeight(d, r.character.skeleton, pose) == doctest::Approx(0.5f).epsilon(1e-4));
    pose[r.elbow].r = bindRot * glm::angleAxis(2.0f, glm::vec3(0, 0, 1));
    CHECK(driverWeight(d, r.character.skeleton, pose) == doctest::Approx(1.0f));  // borné
    pose[r.elbow].r = bindRot * glm::angleAxis(-0.5f, glm::vec3(0, 0, 1));
    CHECK(driverWeight(d, r.character.skeleton, pose) == doctest::Approx(0.0f));
    pose[r.elbow].r = bindRot * glm::angleAxis(0.5f, glm::vec3(1, 0, 0));  // autre axe : sans effet
    CHECK(driverWeight(d, r.character.skeleton, pose) == doctest::Approx(0.0f));

    r.mesh().morphTargets[0].drivers.push_back(d);
    pose[r.elbow].r = bindRot * glm::angleAxis(1.0f, glm::vec3(0, 0, 1));
    pose[r.smile].t.x = 0.25f;
    CHECK(morphWeights(r.mesh(), r.character.skeleton, pose)[0] == doctest::Approx(1.25f));  // poids + pilote
    d.bone = 9999;  // os inexistant : pilote ignoré
    CHECK(driverWeight(d, r.character.skeleton, pose) == 0.0f);
}

TEST_CASE("blend shapes : validation et protection des os de poids") {
    MorphRig r;
    CHECK(r.mesh().validate().ok());
    r.mesh().morphTargets[0].positionDeltas.pop_back();
    CHECK_FALSE(r.mesh().validate().ok());
    r.mesh().morphTargets[0].positionDeltas.push_back({std::nanf(""), 0, 0});
    CHECK_FALSE(r.mesh().validate().ok());
    MorphRig q;
    CHECK_FALSE(removeBoneClean(q.character, q.smile, RemoveMode::WithDescendants).ok());
    CHECK(q.character.skeleton.contains(q.smile));
}

TEST_CASE("blend shapes : aller-retour du format projet") {
    MorphRig r;
    MorphDriver d;
    d.bone = r.elbow;
    d.axis = {0, 0, 1};
    d.angle0 = 0.1f;
    d.angle1 = 1.4f;
    r.mesh().morphTargets[0].drivers.push_back(d);
    io::Project project;
    project.character = r.character;
    const auto loaded = io::parseProject(io::serializeProject(project));
    REQUIRE(loaded.ok());
    const Mesh& back = loaded.value().character.meshes[0];
    REQUIRE(back.morphTargets.size() == 2);
    for (std::size_t t = 0; t < 2; ++t) {
        CHECK(back.morphTargets[t].name == r.mesh().morphTargets[t].name);
        CHECK(back.morphTargets[t].weightBone == r.mesh().morphTargets[t].weightBone);
        REQUIRE(back.morphTargets[t].positionDeltas.size() == 4);
        for (std::size_t v = 0; v < 4; ++v) {
            CHECK(near(back.morphTargets[t].positionDeltas[v], r.mesh().morphTargets[t].positionDeltas[v],
                       1e-6f));
        }
    }
    CHECK(back.morphTargets[0].drivers == r.mesh().morphTargets[0].drivers);
}

TEST_CASE("blend shapes : export -> import glTF conserve cibles, noms, poids de repos et animation") {
    MorphRig r;
    AnimationClip clip;
    clip.name = "talk";
    clip.fps = 30.0f;
    clip.duration = 1.0;
    for (const double t : {0.0, 0.5, 1.0}) {
        Keyframe<glm::vec3> k;
        k.time = t;
        k.value = {static_cast<float>(t), 0, 0};  // poids de « smile » : 0 -> 0,5 -> 1
        k.mode = Interpolation::Linear;
        clip.tracks[r.smile].translation.keys.push_back(k);
    }
    // Un os réel animé, pour que le clip ait aussi une piste de squelette.
    Keyframe<glm::quat> q0, q1;
    q0.time = 0.0;
    q0.value = glm::quat(1, 0, 0, 0);
    q1.time = 1.0;
    q1.value = glm::angleAxis(0.3f, glm::vec3(0, 0, 1));
    q0.mode = q1.mode = Interpolation::Linear;
    clip.tracks[r.elbow].rotation.keys = {q0, q1};
    r.character.clips.push_back(clip);

    const auto path = tempFile("rigger_morph.glb");
    REQUIRE(io::exportGltf(r.character, path).ok());
    auto imported = io::importGltf(path);
    REQUIRE(imported.ok());
    const Character& b = imported.value();
    REQUIRE(b.meshes.size() == 1);
    const Mesh& mesh = b.meshes[0];
    REQUIRE(mesh.morphTargets.size() == 2);
    CHECK(mesh.morphTargets[0].name == "smile");
    CHECK(mesh.morphTargets[1].name == "blink");
    for (std::size_t t = 0; t < 2; ++t) {
        REQUIRE(mesh.morphTargets[t].positionDeltas.size() == 4);
        for (std::size_t v = 0; v < 4; ++v) {
            CHECK(near(mesh.morphTargets[t].positionDeltas[v], r.mesh().morphTargets[t].positionDeltas[v],
                       1e-5f));
        }
        CHECK(mesh.morphTargets[t].weightBone != kNoBone);
        CHECK(hasFlag(b.skeleton.bone(mesh.morphTargets[t].weightBone).flags, BoneFlags::Morph));
    }
    CHECK(b.skeleton.bone(mesh.morphTargets[1].weightBone).bindLocal.t.x ==
          doctest::Approx(0.25f));  // poids de repos
    REQUIRE_FALSE(b.clips.empty());
    const BoneId smileBone = mesh.morphTargets[0].weightBone;
    REQUIRE(b.clips[0].tracks.count(smileBone) == 1);
    const Pose mid = samplePose(b.clips[0], b.skeleton, 0.5);
    CHECK(mid[smileBone].t.x == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(samplePose(b.clips[0], b.skeleton, 1.0)[smileBone].t.x == doctest::Approx(1.0f).epsilon(1e-4));
    // Les os de poids n'existent pas comme nœuds du squelette exporté : un seul vrai os animé reste.
    CHECK(b.skeleton.find("elbow") != kNoBone);
    if (!std::filesystem::exists(tempFile("rigger_keep_files")))
        std::filesystem::remove(path);  // garder le GLB pour le valider à la main
}
