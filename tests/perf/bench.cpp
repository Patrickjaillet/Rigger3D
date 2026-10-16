// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Budgets de performance (ROADMAP phase 15) : 200 os + 200 000 sommets, ouverture d'un projet < 2 s,
// annuler / rétablir < 16 ms. Les budgets ne sont imposés qu'en Release (NDEBUG) ; en Debug les temps sont
// seulement affichés (le code non optimisé est 10 à 30 fois plus lent).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

#include "core/animation/clip.hpp"
#include "core/command/commands.hpp"
#include "core/command/history.hpp"
#include "core/skeleton/fk.hpp"
#include "core/skinning/lbs.hpp"
#include "io/project.hpp"

using namespace rigger;

namespace {

using Clock = std::chrono::steady_clock;

template <class F>
double timeMs(F&& f, int repeat = 1) {
    const auto start = Clock::now();
    for (int i = 0; i < repeat; ++i) f();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / repeat;
}

#ifdef NDEBUG
constexpr bool kEnforce = true;
#else
constexpr bool kEnforce = false;
#endif

void budget(const char* what, double ms, double limit) {
    std::printf("[perf] %-44s %9.3f ms   (budget %.1f ms)%s\n", what, ms, limit,
                kEnforce ? "" : "  [non imposé en Debug]");
    if (kEnforce) CHECK_MESSAGE(ms <= limit, what, " : ", ms, " ms > ", limit, " ms");
}

struct Scene {
    Character character;
    Scene(int bones, int vertices, int keys) {
        Skeleton& sk = character.skeleton;
        std::mt19937 rng(42);
        for (int i = 0; i < bones; ++i) {
            const BoneId parent = i == 0 ? kNoBone : static_cast<BoneId>(rng() % static_cast<unsigned>(i));
            Transform bind;
            bind.t = {0.0f, 0.1f, 0.0f};
            sk.addBone("b" + std::to_string(i), parent, bind);
        }
        Mesh mesh;
        mesh.vertices.resize(static_cast<std::size_t>(vertices));
        mesh.skinWeights.resize(static_cast<std::size_t>(vertices));
        for (int v = 0; v < vertices; ++v) {
            mesh.vertices[static_cast<std::size_t>(v)].position = {static_cast<float>(v % 100) * 0.01f,
                                                                   static_cast<float>(v % 17) * 0.1f, 0.0f};
            SkinWeight& w = mesh.skinWeights[static_cast<std::size_t>(v)];
            for (std::size_t k = 0; k < 4; ++k) {
                w.joints[k] = static_cast<BoneId>(rng() % static_cast<unsigned>(bones));
                w.weights[k] = 0.25f;
            }
        }
        for (int v = 0; v + 2 < vertices; v += 3) {
            mesh.indices.insert(mesh.indices.end(),
                                {static_cast<std::uint32_t>(v), static_cast<std::uint32_t>(v + 1),
                                 static_cast<std::uint32_t>(v + 2)});
        }
        Submesh sub;
        sub.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        mesh.submeshes.push_back(sub);
        character.meshes.push_back(std::move(mesh));
        AnimationClip clip;
        clip.name = "bench";
        clip.fps = 30.0f;
        clip.duration = keys / 30.0;
        for (int b = 0; b < bones; ++b) {
            BoneTracks& t = clip.tracks[static_cast<BoneId>(b)];
            for (int k = 0; k < keys; ++k) {
                Keyframe<glm::quat> r;
                r.time = k / 30.0;
                r.value = glm::angleAxis(0.2f * std::sin(static_cast<float>(k + b)), glm::vec3(0, 0, 1));
                r.mode = Interpolation::Linear;
                t.rotation.keys.push_back(r);
                Keyframe<glm::vec3> p;
                p.time = r.time;
                p.value = {0.0f, 0.1f, 0.0f};
                p.mode = Interpolation::Linear;
                t.translation.keys.push_back(p);
            }
        }
        character.clips.push_back(std::move(clip));
    }
};

}  // namespace

TEST_CASE("perf : évaluation d'une image (200 os, clip de 300 clés par os)") {
    Scene scene(200, 3000, 300);
    const Skeleton& sk = scene.character.skeleton;
    const AnimationClip& clip = scene.character.clips[0];
    Pose pose;
    double t = 0.0;
    budget("sampleClip (200 os x 300 clés)", timeMs([&] { sampleClip(clip, sk, t += 0.0333, pose); }, 200),
           1.0);
    budget("computeGlobals (200 os)", timeMs([&] { (void)computeGlobals(sk, pose); }, 2000), 0.2);
    budget("computeSkinMatrices (200 os)", timeMs([&] { (void)computeSkinMatrices(sk, pose); }, 2000), 0.3);
}

TEST_CASE("perf : skinning CPU de 200 000 sommets (200 os)") {
    Scene scene(200, 200000, 30);
    const Skeleton& sk = scene.character.skeleton;
    Pose pose = sk.bindPose();
    sampleClip(scene.character.clips[0], sk, 0.5, pose);
    const auto matrices = computeSkinMatrices(sk, pose);
    const Mesh& mesh = scene.character.meshes[0];
    // Le rendu interactif utilise le skinning GPU ; le chemin CPU (picking, peinture, export) doit rester
    // fluide.
    budget("skinLbs 200 000 sommets", timeMs([&] { (void)skinLbs(mesh, matrices); }, 5), 16.0);
}

TEST_CASE("perf : ouverture d'un projet (200 os, 200 000 sommets, clip complet)") {
    Scene scene(200, 200000, 300);
    io::Project project;
    project.character = std::move(scene.character);
    std::string text;
    budget("serializeProject", timeMs([&] { text = io::serializeProject(project); }), 2000.0);
    std::printf("[perf] taille du projet : %.1f Mo\n", static_cast<double>(text.size()) / 1e6);
    Result<io::Project> loaded = Error{"unset"};
    budget("parseProject (ouverture)", timeMs([&] { loaded = io::parseProject(text); }), 2000.0);
    CHECK(loaded.ok());
}

TEST_CASE("perf : annuler / rétablir (200 os, 200 commandes)") {
    Scene scene(200, 1000, 10);
    Character& c = scene.character;
    Pose pose = c.skeleton.bindPose();
    EditContext context{&c, &pose};
    History history;
    for (int i = 0; i < 200; ++i) {
        std::vector<SetBoneTransformsCommand::Entry> entries;
        const BoneId id = static_cast<BoneId>(i);
        Transform after = c.skeleton.bone(id).bindLocal;
        after.t.x += 0.01f * static_cast<float>(i + 1);
        entries.push_back({id, c.skeleton.bone(id).bindLocal, after});
        history.execute(std::make_unique<SetBoneTransformsCommand>(SetBoneTransformsCommand::Target::Bind,
                                                                   std::move(entries), "Move"),
                        context);
        history.breakMerge();
    }
    double worstUndo = 0.0, worstRedo = 0.0;
    for (int i = 0; i < 200; ++i) worstUndo = std::max(worstUndo, timeMs([&] { history.undo(context); }));
    for (int i = 0; i < 200; ++i) worstRedo = std::max(worstRedo, timeMs([&] { history.redo(context); }));
    budget("undo (pire cas sur 200)", worstUndo, 16.0);
    budget("redo (pire cas sur 200)", worstRedo, 16.0);
}
