// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include "render/camera.hpp"

TEST_CASE("caméra orbitale : position et zoom cohérents") {
    rigger::render::Camera camera;
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.distance = 5.0f;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;

    const glm::vec3 position = camera.position();
    CHECK(position.x == doctest::Approx(0.0f));
    CHECK(position.y == doctest::Approx(0.0f));
    CHECK(position.z == doctest::Approx(5.0f));

    camera.orbit(glm::radians(90.0f), 0.0f);
    CHECK(camera.position().x == doctest::Approx(5.0f));
    CHECK(camera.position().y == doctest::Approx(0.0f));
    CHECK(camera.position().z == doctest::Approx(0.0f));

    camera.zoom(-2.0f);
    CHECK(camera.distance == doctest::Approx(3.0f));
}

TEST_CASE("caméra : focus et projection orthographique") {
    rigger::render::Camera camera;
    camera.target = {1.0f, 2.0f, 3.0f};
    camera.distance = 10.0f;
    camera.yaw = glm::radians(15.0f);
    camera.pitch = glm::radians(60.0f);
    camera.orthographic = false;

    camera.focusOn({0.0f, 0.5f, 0.0f}, 1.6f);
    CHECK(camera.target.x == doctest::Approx(0.0f));
    CHECK(camera.target.y == doctest::Approx(0.5f));
    CHECK(camera.target.z == doctest::Approx(0.0f));
    CHECK(camera.distance == doctest::Approx(4.0f));
    CHECK(camera.orthographic == false);

    camera.orthographic = true;
    const glm::mat4 ortho = camera.projectionMatrix(2.0f);
    CHECK(ortho[0][0] != doctest::Approx(0.0f));
    CHECK(ortho[1][1] != doctest::Approx(0.0f));
}
