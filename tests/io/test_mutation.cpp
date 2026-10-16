// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Fuzzing déterministe de l'import glTF (en complément des cibles libFuzzer de fuzz/) : des fichiers de test
// légèrement altérés (octets modifiés, troncatures) doivent produire un personnage cohérent ou une erreur,
// jamais un crash.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "io/gltf_import.hpp"
#include "test_helpers.hpp"

using namespace rigger;

namespace {

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeAll(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void importMustNotCrash(const std::filesystem::path& path) {
    const auto result = io::importGltf(path);
    if (result.ok()) {
        for (const Mesh& mesh : result.value().meshes) CHECK(mesh.validate().ok());
    } else {
        CHECK_FALSE(result.error().message.empty());
    }
}

}  // namespace

TEST_CASE("import glTF : fichiers altérés ou tronqués -> erreur ou personnage cohérent, jamais de crash") {
    const std::filesystem::path data(RIGGER_TEST_DATA_DIR);
    testutil::Rng rng(2026);
    for (const char* name : {"arm.gltf", "cube.gltf", "influences.gltf", "sparse.gltf", "RiggedFigure.glb"}) {
        const std::string original = readAll(data / name);
        REQUIRE(!original.empty());
        const bool binary = std::string(name).find(".glb") != std::string::npos;
        const auto path =
            std::filesystem::temp_directory_path() / (binary ? "rigger_mut.glb" : "rigger_mut.gltf");
        // Les fichiers .gltf de test référencent des tampons externes : on les copie à côté du fichier
        // altéré.
        for (const auto& entry : std::filesystem::directory_iterator(data)) {
            if (entry.path().extension() == ".bin") {
                std::filesystem::copy_file(entry.path(),
                                           std::filesystem::temp_directory_path() / entry.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
        for (int trial = 0; trial < 120; ++trial) {
            std::string bytes = original;
            const int edits = rng.integer(1, 6);
            for (int e = 0; e < edits; ++e) {
                bytes[static_cast<std::size_t>(rng.integer(0, static_cast<int>(bytes.size()) - 1))] =
                    static_cast<char>(rng.integer(0, 255));
            }
            writeAll(path, bytes);
            importMustNotCrash(path);
        }
        for (int trial = 0; trial < 30; ++trial) {  // troncatures
            writeAll(path, original.substr(0, static_cast<std::size_t>(
                                                  rng.integer(0, static_cast<int>(original.size()) - 1))));
            importMustNotCrash(path);
        }
        std::filesystem::remove(path);
    }
}
