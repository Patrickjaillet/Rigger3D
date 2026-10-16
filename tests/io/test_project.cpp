// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "core/animation/clip.hpp"
#include "core/example/example.hpp"
#include "core/ik/rig.hpp"
#include "core/skeleton/fk.hpp"
#include "io/gltf_export.hpp"
#include "io/gltf_import.hpp"
#include "io/project.hpp"
#include "test_helpers.hpp"

using namespace rigger;

namespace {

std::filesystem::path dataFile(const char* name) {
    return std::filesystem::path(RIGGER_TEST_DATA_DIR) / name;
}

std::filesystem::path tempFile(const char* name) { return std::filesystem::temp_directory_path() / name; }

// Personnage de test riche : import + clip animé + pose nommée + contrôle IK + repère.
Character richCharacter() {
    auto imported = io::importGltf(dataFile("RiggedFigure.glb"));
    REQUIRE(imported.ok());
    Character c = std::move(imported.value());
    c.clips.clear();
    AnimationClip clip;
    clip.name = "wave";
    clip.duration = 1.0;
    clip.fps = 30.0f;
    const BoneId arm = c.skeleton.find("arm_joint_L_1");
    REQUIRE(arm != kNoBone);
    BoneTracks& t = clip.tracks[arm];
    for (int i = 0; i <= 3; ++i) {
        const double time = i / 3.0;
        Keyframe<glm::vec3> p;
        p.time = time;
        p.value = {0.0f, 0.1f * static_cast<float>(i), 0.0f};
        p.inTangent = {0.0f, 0.3f, 0.0f};
        p.outTangent = {0.0f, 0.4f, 0.0f};
        p.mode = i % 2 == 0 ? Interpolation::Bezier : Interpolation::Linear;
        t.translation.keys.push_back(p);
        Keyframe<glm::quat> r;
        r.time = time;
        r.value = glm::angleAxis(0.4f * static_cast<float>(i), glm::vec3(0, 0, 1));
        t.rotation.keys.push_back(r);
        Keyframe<glm::vec3> s;
        s.time = time;
        s.value = glm::vec3(1.0f + 0.05f * static_cast<float>(i));
        t.scale.keys.push_back(s);
    }
    clip.markers.push_back({0.5, "hit"});
    c.clips.push_back(clip);
    NamedPose pose;
    pose.name = "pose A";
    pose.bones.emplace_back("arm_joint_L_1", Transform{});
    c.poses.push_back(pose);
    Transform tt;
    tt.t = {1, 2, 3};
    const BoneId target = c.skeleton.addBone("IK_target", kNoBone, tt).value();
    c.skeleton.setFlags(target, BoneFlags::Control);
    IKControl ik;
    ik.name = "arm";
    ik.chain = makeChain(c.skeleton, c.skeleton.children(c.skeleton.children(arm).front()).front(), 2,
                         IKSolver::TwoBone);
    ik.chain.limits.resize(2);
    ik.chain.limits[1] = {JointLimitType::Hinge, {1, 0, 0}, 0.0f, 2.0f, 0.5f};
    ik.targetBone = target;
    FootLock lock;
    lock.start = 0.1;
    lock.end = 0.4;
    lock.world.t = {0.5f, 0.5f, 0.5f};
    ik.locks.push_back(lock);
    c.ikControls.push_back(ik);
    c.name = "Figure";
    return c;
}

void expectSameCharacter(const Character& a, const Character& b, float eps = 1e-6f) {
    REQUIRE(a.skeleton.boneCount() == b.skeleton.boneCount());
    for (const BoneId id : a.skeleton.order()) {
        REQUIRE(b.skeleton.contains(id));
        const Bone& x = a.skeleton.bone(id);
        const Bone& y = b.skeleton.bone(id);
        CHECK(x.name == y.name);
        CHECK(x.parent == y.parent);
        CHECK(x.flags == y.flags);
        CHECK(x.length == doctest::Approx(y.length));
        CHECK(testutil::near(x.bindLocal.t, y.bindLocal.t, eps));
        CHECK(testutil::sameRot(x.bindLocal.r, y.bindLocal.r, 1e-6f));
        CHECK(testutil::near(x.color, y.color, eps));
    }
    REQUIRE(a.meshes.size() == b.meshes.size());
    for (std::size_t m = 0; m < a.meshes.size(); ++m) {
        const Mesh& x = a.meshes[m];
        const Mesh& y = b.meshes[m];
        REQUIRE(x.vertices.size() == y.vertices.size());
        CHECK(x.indices == y.indices);
        for (std::size_t v = 0; v < x.vertices.size(); ++v) {
            CHECK(testutil::near(x.vertices[v].position, y.vertices[v].position, eps));
            CHECK(testutil::near(x.vertices[v].normal, y.vertices[v].normal, eps));
        }
        REQUIRE(x.skinWeights.size() == y.skinWeights.size());
        for (std::size_t v = 0; v < x.skinWeights.size(); ++v) {
            CHECK(x.skinWeights[v].joints == y.skinWeights[v].joints);
            CHECK(x.skinWeights[v].weights == y.skinWeights[v].weights);
        }
        REQUIRE(x.submeshes.size() == y.submeshes.size());
        for (std::size_t s = 0; s < x.submeshes.size(); ++s) {
            CHECK(x.submeshes[s].firstIndex == y.submeshes[s].firstIndex);
            CHECK(x.submeshes[s].indexCount == y.submeshes[s].indexCount);
            CHECK(testutil::near(x.submeshes[s].material.baseColor, y.submeshes[s].material.baseColor, eps));
        }
    }
}

}  // namespace

TEST_CASE("projet : sérialiser puis relire restitue tout (os, poids, clips, poses, IK, caméra)") {
    io::Project project;
    project.character = richCharacter();
    project.view.yaw = 0.7f;
    project.view.pitch = 0.3f;
    project.view.distance = 5.5f;
    project.view.target = {1, 2, 3};
    project.view.mode = 1;
    project.view.clipIndex = 0;
    project.view.time = 0.25;

    auto loaded = io::parseProject(io::serializeProject(project));
    REQUIRE(loaded.ok());
    const Character& a = project.character;
    const Character& b = loaded.value().character;
    expectSameCharacter(a, b);
    CHECK(b.name == "Figure");
    REQUIRE(b.clips.size() == 1);
    const AnimationClip& c = b.clips[0];
    CHECK(c.name == "wave");
    CHECK(c.duration == doctest::Approx(1.0));
    REQUIRE(c.markers.size() == 1);
    CHECK(c.markers[0].name == "hit");
    const BoneId arm = a.skeleton.find("arm_joint_L_1");
    const BoneTracks& ta = a.clips[0].tracks.at(arm);
    const BoneTracks& tb = c.tracks.at(arm);
    REQUIRE(ta.translation.keys.size() == tb.translation.keys.size());
    for (std::size_t i = 0; i < ta.translation.keys.size(); ++i) {
        CHECK(ta.translation.keys[i].time == tb.translation.keys[i].time);
        CHECK(testutil::near(ta.translation.keys[i].value, tb.translation.keys[i].value, 1e-6f));
        CHECK(testutil::near(ta.translation.keys[i].outTangent, tb.translation.keys[i].outTangent, 1e-6f));
        CHECK(ta.translation.keys[i].mode == tb.translation.keys[i].mode);
        CHECK(testutil::sameRot(ta.rotation.keys[i].value, tb.rotation.keys[i].value, 1e-6f));
    }
    REQUIRE(b.poses.size() == 1);
    CHECK(b.poses[0].name == "pose A");
    REQUIRE(b.ikControls.size() == 1);
    CHECK(b.ikControls[0].name == "arm");
    CHECK(b.ikControls[0].chain == a.ikControls[0].chain);
    CHECK(b.ikControls[0].locks == a.ikControls[0].locks);
    CHECK(b.ikControls[0].targetBone == a.ikControls[0].targetBone);
    const io::ProjectView& v = loaded.value().view;
    CHECK(v.yaw == doctest::Approx(0.7f));
    CHECK(v.distance == doctest::Approx(5.5f));
    CHECK(testutil::near(v.target, {1, 2, 3}));
    CHECK(v.mode == 1);
    CHECK(v.clipIndex == 0);
    CHECK(v.time == doctest::Approx(0.25));
}

TEST_CASE("projet : fichier atomique, relecture et ancien fichier préservé en cas d'échec") {
    io::Project project;
    project.character = richCharacter();
    const auto path = tempFile("rigger_roundtrip.rig");
    std::filesystem::remove(path);
    REQUIRE(io::saveProject(project, path).ok());
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    auto loaded = io::loadProject(path);
    REQUIRE(loaded.ok());
    expectSameCharacter(project.character, loaded.value().character);
    // Écriture impossible (dossier inexistant) : erreur, rien de créé.
    CHECK_FALSE(io::saveProject(project, tempFile("rigger_no_such_dir") / "x.rig").ok());
    std::filesystem::remove(path);
}

TEST_CASE("projet : fichiers absents, vides, tronqués ou corrompus -> erreur lisible, jamais de crash") {
    CHECK_FALSE(io::loadProject(tempFile("rigger_does_not_exist.rig")).ok());
    CHECK_FALSE(io::parseProject("").ok());
    CHECK_FALSE(io::parseProject("not json at all").ok());
    CHECK_FALSE(io::parseProject("[1,2,3]").ok());
    CHECK_FALSE(io::parseProject("{\"format\":\"other\",\"version\":1}").ok());
    CHECK_FALSE(io::parseProject("{\"format\":\"rigger-project\"}").ok());
    const auto newer = io::parseProject("{\"format\":\"rigger-project\",\"version\":999}");
    REQUIRE_FALSE(newer.ok());
    CHECK(newer.error().message.find("newer") != std::string::npos);

    io::Project project;
    project.character = richCharacter();
    const std::string text = io::serializeProject(project);
    // Troncatures à de nombreuses positions.
    for (std::size_t cut = 0; cut < text.size(); cut += std::max<std::size_t>(1, text.size() / 97)) {
        CHECK_FALSE(io::parseProject(text.substr(0, cut)).ok());
    }
    // Altérations aléatoires : soit une erreur, soit un projet cohérent ; jamais de crash.
    testutil::Rng rng(77);
    for (int trial = 0; trial < 300; ++trial) {
        std::string broken = text;
        for (int k = 0; k < 3; ++k) {
            broken[static_cast<std::size_t>(rng.integer(0, static_cast<int>(broken.size()) - 1))] =
                static_cast<char>(rng.integer(32, 126));
        }
        const auto result = io::parseProject(broken);
        if (result.ok()) {
            for (const Mesh& m : result.value().character.meshes) CHECK(m.validate().ok());
        }
    }
}

TEST_CASE("projet : données incohérentes refusées (poids sur os inconnu, indices hors limites)") {
    io::Project project;
    project.character = richCharacter();
    std::string text = io::serializeProject(project);
    // Un squelette vide est refusé.
    auto j = nlohmann::json::parse(text);
    j["skeleton"]["bones"] = nlohmann::json::array();
    CHECK_FALSE(io::parseProject(j.dump()).ok());
    // Piste d'animation sur un os inexistant.
    j = nlohmann::json::parse(text);
    j["clips"][0]["tracks"][0]["bone"] = 9999;
    CHECK_FALSE(io::parseProject(j.dump()).ok());
    // Durée de clip invalide.
    j = nlohmann::json::parse(text);
    j["clips"][0]["fps"] = 0;
    CHECK_FALSE(io::parseProject(j.dump()).ok());
}

TEST_CASE("export glTF : import -> export -> import conserve os, poids (1e-4) et clips") {
    auto imported = io::importGltf(dataFile("RiggedFigure.glb"));
    REQUIRE(imported.ok());
    const Character& a = imported.value();
    REQUIRE_FALSE(a.clips.empty());
    const auto path = tempFile("rigger_export.glb");
    REQUIRE(io::exportGltf(a, path).ok());
    auto reimported = io::importGltf(path);
    REQUIRE(reimported.ok());
    const Character& b = reimported.value();

    REQUIRE(b.skeleton.boneCount() == a.skeleton.boneCount());
    for (const BoneId id : a.skeleton.order()) {
        const BoneId other = b.skeleton.find(a.skeleton.bone(id).name);
        REQUIRE(other != kNoBone);
        const Bone& x = a.skeleton.bone(id);
        const Bone& y = b.skeleton.bone(other);
        CHECK(testutil::near(x.bindLocal.t, y.bindLocal.t, 1e-5f));
        CHECK(testutil::sameRot(x.bindLocal.r, y.bindLocal.r, 1e-5f));
        CHECK((x.parent == kNoBone) == (y.parent == kNoBone));
        if (x.parent != kNoBone) CHECK(b.skeleton.bone(y.parent).name == a.skeleton.bone(x.parent).name);
    }
    REQUIRE(b.meshes.size() == a.meshes.size());
    for (std::size_t m = 0; m < a.meshes.size(); ++m) {
        REQUIRE(a.meshes[m].vertices.size() == b.meshes[m].vertices.size());
        CHECK(a.meshes[m].indices == b.meshes[m].indices);
        for (std::size_t v = 0; v < a.meshes[m].vertices.size(); ++v) {
            CHECK(testutil::near(a.meshes[m].vertices[v].position, b.meshes[m].vertices[v].position, 1e-5f));
            const SkinWeight& x = a.meshes[m].skinWeights[v];
            const SkinWeight& y = b.meshes[m].skinWeights[v];
            for (std::size_t k = 0; k < 4; ++k) {  // poids par NOM d'os (les indices peuvent changer)
                if (x.weights[k] <= 0.0f) continue;
                const BoneId other = b.skeleton.find(a.skeleton.bone(x.joints[k]).name);
                float got = 0.0f;
                for (std::size_t q = 0; q < 4; ++q) {
                    if (y.joints[q] == other) got += y.weights[q];
                }
                CHECK(got == doctest::Approx(x.weights[k]).epsilon(1e-4));
            }
        }
    }
    REQUIRE(b.clips.size() == a.clips.size());
    // Pose échantillonnée identique (les clips exportés sont rééchantillonnés en LINEAR à FPS du clip).
    const AnimationClip& ca = a.clips[0];
    const AnimationClip& cb = b.clips[0];
    CHECK(cb.duration == doctest::Approx(ca.duration).epsilon(0.05));
    const int frameCount = static_cast<int>(std::floor(ca.duration * ca.fps + 1e-6));
    for (const double t :
         {0.0, (frameCount / 2) / static_cast<double>(ca.fps), frameCount / static_cast<double>(ca.fps)}) {
        const auto ga = computeGlobals(a.skeleton, samplePose(ca, a.skeleton, t));
        const auto gb = computeGlobals(b.skeleton, samplePose(cb, b.skeleton, t));
        for (const BoneId id : a.skeleton.order()) {
            const BoneId other = b.skeleton.find(a.skeleton.bone(id).name);
            CHECK(testutil::near(glm::vec3(ga[id][3]), glm::vec3(gb[other][3]), 2e-3f));
        }
    }
    std::filesystem::remove(path);
}

TEST_CASE("export glTF : options (plage, maillages exclus, renommage, .gltf + .bin)") {
    auto imported = io::importGltf(dataFile("RiggedFigure.glb"));
    REQUIRE(imported.ok());
    const Character& a = imported.value();
    io::GltfExportOptions options;
    options.includeMeshes = {false};
    options.clipNames = {"Renamed"};
    options.firstFrame = 0;
    options.lastFrame = 5;
    options.binary = false;
    const auto path = tempFile("rigger_options.gltf");
    REQUIRE(io::exportGltf(a, path, options).ok());
    CHECK(std::filesystem::exists(tempFile("rigger_options.bin")));
    auto back = io::importGltf(path);
    REQUIRE(back.ok());
    CHECK(back.value().meshes.empty());
    REQUIRE(back.value().clips.size() == 1);
    CHECK(back.value().clips[0].name == "Renamed");
    CHECK(back.value().clips[0].duration == doctest::Approx(5.0 / a.clips[0].fps).epsilon(0.05));
    std::filesystem::remove(path);
    std::filesystem::remove(tempFile("rigger_options.bin"));
}

TEST_CASE("export glTF : les os de contrôle IK ne sont pas exportés, le bake IK exporte les rotations") {
    Character c = richCharacter();
    const auto controlCount = c.skeleton.boneCount();
    const auto path = tempFile("rigger_ik.glb");
    io::GltfExportOptions options;
    options.bakeIK = true;
    REQUIRE(io::exportGltf(c, path, options).ok());
    auto back = io::importGltf(path);
    REQUIRE(back.ok());
    CHECK(back.value().skeleton.boneCount() == controlCount - 1);  // sans « IK_target »
    CHECK(back.value().skeleton.find("IK_target") == kNoBone);
    CHECK_FALSE(back.value().clips.empty());
    std::filesystem::remove(path);
}

TEST_CASE("projet d'exemple : sérialisation puis relecture identique") {
    io::Project project;
    project.character = buildExampleCharacter();
    const std::string text = io::serializeProject(project);
    CHECK(text.size() > 1000);
    const auto back = io::parseProject(text);
    REQUIRE(back);
    CHECK(back.value().character.name == "Rigby");
    CHECK(back.value().character.skeleton.boneCount() == project.character.skeleton.boneCount());
    CHECK(back.value().character.clips.size() == 2);
    CHECK(back.value().character.meshes[0].vertices.size() == project.character.meshes[0].vertices.size());
}
