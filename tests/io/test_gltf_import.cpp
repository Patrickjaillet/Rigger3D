// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/skeleton/fk.hpp"
#include "core/skinning/lbs.hpp"
#include "io/gltf_import.hpp"

using namespace rigger;

namespace {

std::filesystem::path dataFile(const char* name) {
    return std::filesystem::path(RIGGER_TEST_DATA_DIR) / name;
}

bool hasWarning(const Character& c, const std::string& needle) {
    return std::any_of(c.warnings.begin(), c.warnings.end(),
                       [&](const std::string& w) { return w.find(needle) != std::string::npos; });
}

// Charge un fichier de test ; échoue le test (sans exception) si l'import est refusé.
Character load(const char* name) {
    auto result = io::importGltf(dataFile(name));
    if (!result) {
        FAIL_CHECK(name << ": " << result.error().message);
        return {};
    }
    return std::move(result.value());
}

void checkValid(const Character& c) {
    for (const Mesh& mesh : c.meshes) {
        CHECK(mesh.validate().ok());
    }
}

}  // namespace

TEST_CASE("import : cube statique") {
    const Character c = load("cube.gltf");
    CHECK(c.skeleton.boneCount() == 0);
    CHECK(c.clips.empty());
    REQUIRE(c.meshes.size() == 1);
    const Mesh& mesh = c.meshes[0];
    CHECK(mesh.vertices.size() == 8);
    CHECK(mesh.indices.size() == 36);
    CHECK_FALSE(mesh.hasSkinning());
    REQUIRE(mesh.submeshes.size() == 1);
    CHECK(mesh.submeshes[0].material.baseColor.r == doctest::Approx(1.0f));
    CHECK(mesh.submeshes[0].material.baseColor.g == doctest::Approx(0.0f));
    // La translation du nœud (0,1,0) est figée dans les sommets d'un maillage statique.
    const Bounds b = mesh.computeBounds();
    CHECK(b.min.y == doctest::Approx(0.5f));
    CHECK(b.max.y == doctest::Approx(1.5f));
    // Normales absentes du fichier : recalculées, unitaires.
    for (const Vertex& v : mesh.vertices) {
        CHECK(glm::length(v.normal) == doctest::Approx(1.0f).epsilon(1e-4));
    }
    checkValid(c);
}

TEST_CASE("import : bras à 2 os (byteStride, joints UBYTE, poids USHORT normalisés)") {
    const Character c = load("arm.gltf");
    REQUIRE(c.skeleton.boneCount() == 2);
    const BoneId shoulder = c.skeleton.find("shoulder");
    const BoneId elbow = c.skeleton.find("elbow");
    REQUIRE(shoulder != kNoBone);
    REQUIRE(elbow != kNoBone);
    CHECK(c.skeleton.bone(elbow).parent == shoulder);
    CHECK(c.skeleton.bone(elbow).bindLocal.t.y == doctest::Approx(1.0f));
    CHECK(c.skeleton.bone(shoulder).length == doctest::Approx(1.0f));
    CHECK(c.warnings.empty());

    REQUIRE(c.meshes.size() == 1);
    const Mesh& mesh = c.meshes[0];
    REQUIRE(mesh.vertices.size() == 30);
    CHECK(mesh.indices.size() == 144);
    REQUIRE(mesh.hasSkinning());
    checkValid(c);

    // Anneau 0 (y=0) : 100 % épaule ; anneau 2 (y=1) : 50/50 ; anneau 4 (y=2) : 100 % coude.
    const auto weightOn = [&](std::size_t vertex, BoneId bone) {
        const SkinWeight& s = mesh.skinWeights[vertex];
        float w = 0.0f;
        for (std::size_t k = 0; k < 4; ++k) {
            if (s.joints[k] == bone) {
                w += s.weights[k];
            }
        }
        return w;
    };
    CHECK(mesh.vertices[0].position.y == doctest::Approx(0.0f));
    CHECK(weightOn(0, shoulder) == doctest::Approx(1.0f));
    CHECK(weightOn(12, shoulder) == doctest::Approx(0.5f).epsilon(1e-3));
    CHECK(weightOn(12, elbow) == doctest::Approx(0.5f).epsilon(1e-3));
    CHECK(weightOn(24, elbow) == doctest::Approx(1.0f));
    // Les normales entrelacées (stride 24, offset 12) sont lues correctement.
    CHECK(mesh.vertices[0].normal.x == doctest::Approx(1.0f));
    CHECK(mesh.vertices[0].position.x == doctest::Approx(0.1f));
}

TEST_CASE("import : animation du bras (LINEAR, STEP, CUBICSPLINE)") {
    const Character c = load("arm.gltf");
    REQUIRE(c.clips.size() == 1);
    const AnimationClip& clip = c.clips[0];
    CHECK(clip.name == "Bend");
    CHECK(clip.duration == doctest::Approx(1.0));
    const BoneId shoulder = c.skeleton.find("shoulder");
    const BoneId elbow = c.skeleton.find("elbow");

    Pose pose;
    sampleClip(clip, c.skeleton, 0.0, pose);
    CHECK(pose[elbow].r.w == doctest::Approx(1.0f));

    // Rotation LINEAR : à t = 0.25 s, moitié du chemin entre 0° et 45°, soit 22,5° autour de Z.
    sampleClip(clip, c.skeleton, 0.25, pose);
    const glm::quat expected = glm::angleAxis(glm::radians(22.5f), glm::vec3(0, 0, 1));
    CHECK(std::fabs(glm::dot(pose[elbow].r, expected)) == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(pose[elbow].t.y == doctest::Approx(1.0f));  // translation non animée : bind

    // Translation STEP de l'épaule : saute à 0.5 s.
    sampleClip(clip, c.skeleton, 0.49, pose);
    CHECK(pose[shoulder].t.x == doctest::Approx(0.0f));
    sampleClip(clip, c.skeleton, 0.75, pose);
    CHECK(pose[shoulder].t.x == doctest::Approx(0.5f));

    // Échelle CUBICSPLINE (tangentes nulles) : 1 -> 2 en 0.5 s, milieu = 1.5.
    sampleClip(clip, c.skeleton, 0.25, pose);
    CHECK(pose[shoulder].s.x == doctest::Approx(1.5f));

    // Fin (t = 1 s) : coude plié à 90° ; le bout de l'avant-bras (0,1,0 dans le repère du coude) passe en
    // (-1,1,0).
    AnimationClip once = clip;
    once.loop = false;
    sampleClip(once, c.skeleton, 1.0, pose);
    const auto globals = computeGlobals(c.skeleton, pose);
    const glm::vec3 tip = glm::vec3(globals[elbow] * glm::vec4(0, 1, 0, 1));
    CHECK(tip.x == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(tip.y == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("import : accessor sparse") {
    const Character c = load("sparse.gltf");
    REQUIRE(c.meshes.size() == 1);
    const Mesh& mesh = c.meshes[0];
    REQUIRE(mesh.vertices.size() == 4);
    CHECK(mesh.vertices[2].position.x == doctest::Approx(1.0f));
    CHECK(mesh.vertices[2].position.y == doctest::Approx(1.0f));
    CHECK(mesh.vertices[3].position.y == doctest::Approx(1.0f));
    CHECK(mesh.indices.size() == 6);
    checkValid(c);
}

TEST_CASE("import : plus de 4 influences -> les 4 plus fortes, normalisées, avec avertissement") {
    const Character c = load("influences.gltf");
    REQUIRE(c.meshes.size() == 1);
    CHECK(c.skeleton.boneCount() == 6);
    CHECK(hasWarning(c, "more than 4"));
    const SkinWeight& s = c.meshes[0].skinWeights[0];
    const float sum = 0.3f + 0.25f + 0.2f + 0.1f;
    CHECK(s.weights[0] == doctest::Approx(0.3f / sum));
    CHECK(s.weights[1] == doctest::Approx(0.25f / sum));
    CHECK(s.weights[2] == doctest::Approx(0.2f / sum));
    CHECK(s.weights[3] == doctest::Approx(0.1f / sum));
    CHECK(s.joints[0] == c.skeleton.find("j0"));
    checkValid(c);
}

TEST_CASE("import : personnages Khronos (RiggedFigure, CesiumMan)") {
    for (const char* name : {"RiggedFigure.glb", "CesiumMan.glb"}) {
        CAPTURE(name);
        const Character c = load(name);
        CHECK(c.skeleton.boneCount() > 1);
        REQUIRE_FALSE(c.meshes.empty());
        REQUIRE_FALSE(c.clips.empty());
        checkValid(c);
        for (const Mesh& mesh : c.meshes) {
            CHECK(mesh.hasSkinning());
            CHECK(mesh.vertices.size() > 100);
            for (const SkinWeight& s : mesh.skinWeights) {
                for (std::size_t k = 0; k < 4; ++k) {
                    if (s.weights[k] > 0.0f) {
                        CHECK(c.skeleton.contains(s.joints[k]));
                    }
                }
            }
        }

        // L'animation embarquée déplace réellement les os : la pose diffère du repos à mi-parcours.
        const AnimationClip& clip = c.clips[0];
        CHECK(clip.duration > 0.0);
        Pose rest = c.skeleton.bindPose();
        Pose moved;
        sampleClip(clip, c.skeleton, clip.duration * 0.37, moved);
        float maxDiff = 0.0f;
        const auto restGlobals = computeGlobals(c.skeleton, rest);
        const auto movedGlobals = computeGlobals(c.skeleton, moved);
        for (const BoneId id : c.skeleton.order()) {
            maxDiff = std::max(maxDiff,
                               glm::length(glm::vec3(restGlobals[id][3]) - glm::vec3(movedGlobals[id][3])));
        }
        CHECK(maxDiff > 1e-3f);

        // Le skinning de la pose animée reste cohérent avec le maillage de repos (pas d'explosion :
        // vérifie aussi que le repère de bind du fichier a été correctement repris).
        const auto skinMatrices = computeSkinMatrices(c.skeleton, moved);
        for (const Mesh& mesh : c.meshes) {
            const auto skinned = skinLbs(mesh, skinMatrices);
            REQUIRE(skinned.ok());
            Mesh deformed = mesh;
            for (std::size_t v = 0; v < deformed.vertices.size(); ++v) {
                deformed.vertices[v].position = skinned.value()[v].position;
            }
            const Bounds restBounds = mesh.computeBounds();
            const Bounds nowBounds = deformed.computeBounds();
            // Le personnage reste debout : la rotation Z-up -> Y-up des ancêtres du squelette n'est pas
            // perdue quand l'animation remplace la transformation de l'os racine.
            CHECK(nowBounds.center.y > restBounds.min.y + 0.25f * (restBounds.max.y - restBounds.min.y));
            CHECK(nowBounds.center.y < restBounds.max.y);
            CHECK(restBounds.radius > 0.1f);
            CHECK(nowBounds.radius < restBounds.radius * 2.0f);
            CHECK(glm::length(nowBounds.center - restBounds.center) < restBounds.radius * 2.0f);
        }
    }
}

TEST_CASE("import : erreurs explicites") {
    SUBCASE("fichier absent") {
        const auto r = io::importGltf(dataFile("does_not_exist.glb"));
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().message.find("not found") != std::string::npos);
    }
    SUBCASE("extension non supportée") {
        const auto r = io::importGltf(dataFile("CesiumMan.LICENSE.md"));
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().message.find("Unsupported file type") != std::string::npos);
    }
    SUBCASE("contenu invalide") {
        const auto path = std::filesystem::temp_directory_path() / "rigger_invalid.gltf";
        std::ofstream(path) << "this is not json";
        const auto r = io::importGltf(path);
        std::filesystem::remove(path);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().message.find("Cannot read glTF") != std::string::npos);
    }
    SUBCASE("version non supportée") {
        const auto path = std::filesystem::temp_directory_path() / "rigger_v1.gltf";
        std::ofstream(path) << R"({"asset":{"version":"1.0"}})";
        const auto r = io::importGltf(path);
        std::filesystem::remove(path);
        REQUIRE_FALSE(r.ok());
    }
    SUBCASE("buffer externe manquant") {
        const auto path = std::filesystem::temp_directory_path() / "rigger_missing_bin.gltf";
        std::ofstream(path)
            << R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":12,"uri":"nope.bin"}],)"
               R"("bufferViews":[{"buffer":0,"byteLength":12}],)"
               R"("accessors":[{"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"}],)"
               R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
               R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})";
        const auto r = io::importGltf(path);
        std::filesystem::remove(path);
        REQUIRE_FALSE(r.ok());
    }
}

TEST_CASE("import : progression croissante et annulation") {
    std::vector<float> fractions;
    const auto ok = io::importGltf(dataFile("arm.gltf"), [&](float f, std::string_view) {
        fractions.push_back(f);
        return true;
    });
    REQUIRE(ok.ok());
    REQUIRE(fractions.size() >= 4);
    CHECK(std::is_sorted(fractions.begin(), fractions.end()));
    CHECK(fractions.front() == doctest::Approx(0.0f));
    CHECK(fractions.back() == doctest::Approx(1.0f));

    int calls = 0;
    const auto cancelled =
        io::importGltf(dataFile("arm.gltf"), [&](float, std::string_view) { return ++calls < 3; });
    REQUIRE_FALSE(cancelled.ok());
    CHECK(cancelled.error().message == io::kImportCancelled);
}
