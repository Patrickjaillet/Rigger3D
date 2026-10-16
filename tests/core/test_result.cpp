// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include <string>

#include "core/result.hpp"

using rigger::Error;
using rigger::Result;

TEST_CASE("Result<T> contient une valeur") {
    Result<int> r = 42;
    REQUIRE(r.ok());
    CHECK(r.value() == 42);
}

TEST_CASE("Result<T> contient une erreur") {
    Result<std::string> r = Error{"fichier introuvable"};
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().message == "fichier introuvable");
}

TEST_CASE("Result<void>") {
    Result<void> good;
    CHECK(good.ok());
    Result<void> bad = Error{"échec"};
    CHECK_FALSE(bad.ok());
    CHECK(bad.error().message == "échec");
}
