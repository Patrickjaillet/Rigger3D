// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Cible libFuzzer : chargeur de projets .rig. Toute entrée doit produire un projet valide ou une erreur,
// jamais de crash.
#include <cstddef>
#include <cstdint>
#include <string>

#include "io/project.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);
    const auto project = rigger::io::parseProject(text);
    if (project.ok()) {
        // Un projet accepté doit se re-sérialiser et se relire.
        const auto again = rigger::io::parseProject(rigger::io::serializeProject(project.value()));
        if (!again.ok()) __builtin_trap();
    }
    return 0;
}
