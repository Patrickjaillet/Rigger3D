// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <array>

#include "core/mesh/mesh.hpp"
#include "core/skinning/weights.hpp"
#include "test_helpers.hpp"

using namespace rigger;

TEST_CASE("mesh vide : validation et bornes neutres") {
    Mesh mesh;
    CHECK(mesh.validate().ok());
    CHECK(mesh.computeBounds().radius == 0.0f);
}

TEST_CASE("mesh triangulaire : recalcul des normales et bornes") {
    Mesh mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0.0f, 0.0f, 0.0f};
    mesh.vertices[1].position = {1.0f, 0.0f, 0.0f};
    mesh.vertices[2].position = {0.0f, 1.0f, 0.0f};
    mesh.indices = {0, 1, 2};

    mesh.recomputeNormals();

    CHECK_VEC3(mesh.vertices[0].normal, glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK_VEC3(mesh.vertices[1].normal, glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK_VEC3(mesh.vertices[2].normal, glm::vec3(0.0f, 0.0f, 1.0f));

    const Bounds bounds = mesh.computeBounds();
    CHECK_VEC3(bounds.min, glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK_VEC3(bounds.max, glm::vec3(1.0f, 1.0f, 0.0f));
    CHECK(testutil::near(glm::vec3(bounds.center), glm::vec3(0.5f, 0.5f, 0.0f), 1e-4f));
    CHECK(bounds.radius > 0.0f);
}

TEST_CASE("skin weights : validation des poids et normalisation") {
    Mesh mesh;
    mesh.vertices.resize(1);
    mesh.skinWeights.resize(1);
    mesh.skinWeights[0].joints = {0u, 1u, kNoBone, kNoBone};
    mesh.skinWeights[0].weights = {0.6f, 0.4f, 0.0f, 0.0f};

    CHECK(mesh.validate().ok());

    REQUIRE(normalizeWeights(mesh.skinWeights[0]).ok());
    CHECK(mesh.skinWeights[0].weights[0] > 0.0f);
    CHECK(mesh.skinWeights[0].weights[1] > 0.0f);
    CHECK(std::fabs(mesh.skinWeights[0].weights[0] + mesh.skinWeights[0].weights[1] - 1.0f) < 1e-5f);

    mesh.skinWeights[0].weights = {0.25f, 0.25f, 0.25f, 0.25f};
    mesh.skinWeights[0].joints = {0u, 1u, 2u, 3u};
    CHECK(mesh.validate().ok());

    mesh.skinWeights[0].weights = {0.25f, 0.25f, 0.25f, 0.25f};
    mesh.skinWeights[0].joints = {0u, kNoBone, 2u, 3u};
    CHECK_FALSE(mesh.validate().ok());

    mesh.skinWeights[0].weights = {0.5f, 0.4f, 0.2f, 0.05f};
    mesh.skinWeights[0].joints = {0u, 1u, 2u, 3u};
    CHECK_FALSE(mesh.validate().ok());
}
