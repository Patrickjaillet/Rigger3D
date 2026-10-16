// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/version.hpp"

namespace rigger {

std::string versionBanner() {
    std::string s(kAppName);
    s += " v";
    s += kVersionString;
    return s;
}

}  // namespace rigger
