// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <glm/gtc/matrix_transform.hpp>

#include "core/skinning/dqs.hpp"
#include "core/skinning/lbs.hpp"
#include "core/skinning/weights.hpp"
#include "test_helpers.hpp"

using namespace rigger;

namespace {

Mesh makeOneVertex(SkinWeight skin) {
    Mesh mesh;
    mesh.vertices.resize(1);
    mesh.vertices[0].position = {1.0f, 0.0f, 0.0f};
    mesh.vertices[0].normal = {1.0f, 1.0f, 0.0f};
    mesh.skinWeights.push_back(skin);
    return mesh;
}

}  // namespace

TEST_CASE("LBS : un sommet suit exactement sa matrice et transforme la normale") {
    SkinWeight skin;
    skin.joints = {0u, kNoBone, kNoBone, kNoBone};
    skin.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    const Mesh mesh = makeOneVertex(skin);

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(2.0f, 0.0f, 0.0f));
    transform = glm::scale(transform, glm::vec3(2.0f, 1.0f, 1.0f));
    auto result = skinLbs(mesh, {transform});

    REQUIRE(result.ok());
    CHECK_VEC3(result.value()[0].position, glm::vec3(4.0f, 0.0f, 0.0f));
    const glm::vec3 expected = glm::normalize(glm::vec3(0.5f, 1.0f, 0.0f));
    CHECK_VEC3(result.value()[0].normal, expected);
}

TEST_CASE("LBS : deux influences à 90 degrés donnent la moyenne matricielle") {
    SkinWeight skin;
    skin.joints = {0u, 1u, kNoBone, kNoBone};
    skin.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    const Mesh mesh = makeOneVertex(skin);
    const glm::mat4 identity(1.0f);
    const glm::mat4 quarterTurn = glm::rotate(identity, glm::half_pi<float>(), glm::vec3(0, 0, 1));

    auto result = skinLbs(mesh, {identity, quarterTurn});

    REQUIRE(result.ok());
    CHECK_VEC3(result.value()[0].position, glm::vec3(0.5f, 0.5f, 0.0f));
}

TEST_CASE("DQS : mélange de rotations opposées conserve mieux le volume que LBS") {
    SkinWeight skin;
    skin.joints = {0u, 1u, kNoBone, kNoBone};
    skin.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    const Mesh mesh = makeOneVertex(skin);
    const glm::mat4 identity(1.0f);
    const glm::mat4 positive = glm::rotate(identity, glm::half_pi<float>(), glm::vec3(0, 0, 1));
    const glm::mat4 negative = glm::rotate(identity, -glm::half_pi<float>(), glm::vec3(0, 0, 1));

    auto dqs = skinDqs(mesh, {positive, negative});

    REQUIRE(dqs.ok());
    const glm::vec3 linearBlend = 0.5f * (glm::vec3(positive * glm::vec4(mesh.vertices[0].position, 1.0f)) +
                                          glm::vec3(negative * glm::vec4(mesh.vertices[0].position, 1.0f)));
    CHECK(glm::length(linearBlend) < 1.0e-4f);
    CHECK_VEC3(dqs.value()[0].position, mesh.vertices[0].position);
}

TEST_CASE("DQS : refuse une transformation non rigide") {
    SkinWeight skin;
    skin.joints = {0u, kNoBone, kNoBone, kNoBone};
    skin.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    const Mesh mesh = makeOneVertex(skin);

    auto result = skinDqs(mesh, {glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 1.0f))});

    CHECK_FALSE(result.ok());
}

TEST_CASE("weights : normalisation, limitation et pruning préservent les invariants") {
    SkinWeight skin;
    skin.joints = {2u, 3u, 4u, 5u};
    skin.weights = {0.1f, 0.2f, 0.3f, 0.4f};

    REQUIRE(limitInfluences(skin, 2).ok());
    CHECK(skin.joints[0] == 5u);
    CHECK(skin.joints[1] == 4u);
    CHECK(testutil::near(glm::vec3(skin.weights[0], skin.weights[1], 0.0f),
                         glm::vec3(4.0f / 7.0f, 3.0f / 7.0f, 0.0f)));

    REQUIRE(pruneWeights(skin, 0.5f).ok());
    CHECK(skin.weights[0] == doctest::Approx(1.0f));
    CHECK(skin.weights[1] == 0.0f);
    CHECK(skin.joints[1] == kNoBone);
}

TEST_CASE("weights : opérations invalides échouent sans modifier les poids") {
    SkinWeight skin;
    skin.joints = {0u, 1u, kNoBone, kNoBone};
    skin.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    const SkinWeight original = skin;

    CHECK_FALSE(limitInfluences(skin, 0).ok());
    CHECK_FALSE(pruneWeights(skin, 1.0f).ok());
    CHECK(skin.weights == original.weights);
    CHECK(skin.joints == original.joints);
}
