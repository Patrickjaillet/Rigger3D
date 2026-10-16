// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Cible libFuzzer : import glTF / GLB. Le fichier d'entrée est écrit dans un fichier temporaire (l'import lit
// un chemin).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "io/gltf_import.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const std::filesystem::path glb = std::filesystem::temp_directory_path() / "rigger_fuzz.glb";
    static const std::filesystem::path gltf = std::filesystem::temp_directory_path() / "rigger_fuzz.gltf";
    // GLB si l'entrée commence par la magie « glTF », glTF JSON sinon.
    const bool binary = size >= 4 && data[0] == 'g' && data[1] == 'l' && data[2] == 'T' && data[3] == 'F';
    const auto& path = binary ? glb : gltf;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    const auto result = rigger::io::importGltf(path);
    if (result.ok()) {
        for (const auto& mesh : result.value().meshes) {
            if (!mesh.validate().ok()) __builtin_trap();  // un import accepté doit être cohérent
        }
    }
    return 0;
}
