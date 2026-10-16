// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <doctest/doctest.h>

#include "core/version.hpp"

TEST_CASE("version") {
    CHECK(rigger::kAppName == "Rigger");
    CHECK(rigger::kVersionMajor >= 0);
    CHECK(rigger::versionBanner() == "Rigger v" + std::string(rigger::kVersionString));
    CHECK(rigger::versionBanner().rfind("Rigger v", 0) == 0);
}
