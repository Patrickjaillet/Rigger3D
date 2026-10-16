// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <cmath>

#include "core/example/example.hpp"
#include "core/skeleton/fk.hpp"
#include "test_helpers.hpp"

using namespace rigger;

TEST_CASE("personnage d'exemple : squelette, maillage skinné, deux clips en boucle") {
    const Character c = buildExampleCharacter();
    CHECK(c.name == "Rigby");
    CHECK(c.skeleton.boneCount() == 19);
    REQUIRE(c.meshes.size() == 1);
    const Mesh& mesh = c.meshes[0];
    CHECK(mesh.validate().ok());
    CHECK(mesh.vertices.size() > 300);
    CHECK(mesh.submeshes.size() == 3);
    for (const SkinWeight& w : mesh.skinWeights) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < 4; ++k) {
            sum += w.weights[k];
            if (w.weights[k] > 0.0f) CHECK(c.skeleton.contains(w.joints[k]));
        }
        CHECK(sum == doctest::Approx(1.0f));
    }
    REQUIRE(c.clips.size() == 2);
    CHECK(c.clips[0].name == "Wave");
    CHECK(c.clips[1].name == "Walk");
    for (const AnimationClip& clip : c.clips) {
        CHECK(clip.fps == doctest::Approx(30.0f));
        CHECK(clip.duration == doctest::Approx(1.0));
        CHECK(clip.loop);
        CHECK_FALSE(clip.tracks.empty());
    }
    // « Wave » lève vraiment le bras : la main monte au-dessus de l'épaule.
    const BoneId hand = c.skeleton.find("hand.L"), shoulder = c.skeleton.find("upperarm.L");
    const auto bind = computeGlobals(c.skeleton, c.skeleton.bindPose());
    const auto waved = computeGlobals(c.skeleton, samplePose(c.clips[0], c.skeleton, 0.0));
    CHECK(waved[hand][3][1] > bind[hand][3][1] + 0.3f);
    CHECK(waved[shoulder][3][1] == doctest::Approx(bind[shoulder][3][1]));
    // Le mesh est dans la pose de repos : sommets dans la boîte du personnage (1,8 m).
    const Bounds b = mesh.computeBounds();
    CHECK(b.max.y < 1.9f);
    CHECK(b.min.y > -0.1f);
}
