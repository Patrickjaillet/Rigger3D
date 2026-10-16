// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <optional>
#include <string>

#include "core/character/character.hpp"
#include "core/result.hpp"

namespace rigger::io {

// Projet Rigger (.rig) : un fichier JSON UTF-8 unique (schéma décrit dans docs/file_format.md). Les gros
// tampons (sommets, indices, poids, textures) sont des blocs binaires little-endian encodés en base64 dans le
// JSON.
inline constexpr int kProjectVersion = 1;
inline constexpr const char* kProjectExtension = ".rig";

// Caméra et préférences de scène sauvegardées avec le projet (facultatives à la lecture).
struct ProjectView {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 4.0f;
    glm::vec3 target{0.0f};
    bool orthographic = false;
    int mode = 2;  // 0 Rig, 1 Skin, 2 Animate
    int clipIndex = -1;
    double time = 0.0;
    std::string sourcePath;  // fichier glTF d'origine (informatif)
};

struct Project {
    Character character;
    ProjectView view;
};

// Écrit le projet de façon atomique (fichier temporaire puis renommage) : un échec ne détruit jamais l'ancien
// fichier.
Result<void> saveProject(const Project& project, const std::filesystem::path& path);

// Lit un projet. Fichier absent, tronqué, corrompu, version inconnue ou données incohérentes : erreur
// lisible, jamais de crash. Les projets de version antérieure sont migrés (migrateProject).
Result<Project> loadProject(const std::filesystem::path& path);

// Sérialisation en mémoire (utilisée par l'autosave et les tests).
std::string serializeProject(const Project& project);
Result<Project> parseProject(const std::string& text);

}  // namespace rigger::io
