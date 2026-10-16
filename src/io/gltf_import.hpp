// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <filesystem>
#include <functional>
#include <string_view>

#include "core/character/character.hpp"
#include "core/result.hpp"

namespace rigger::io {

// Importe un fichier glTF 2.0 (.gltf ou .glb) : squelette (reconstruit depuis les `skins`), maillages
// skinnés (4 influences max, poids normalisés), matériaux PBR de base et animations (TRS).
// Les problèmes non bloquants (influences > 4, os sans skin ignorés, IBM incohérentes…) sont listés dans
// Character::warnings ; les erreurs bloquantes (fichier absent, version, extension requise non gérée,
// buffers manquants ou hors limites) sont renvoyées avec un message lisible par l'utilisateur.
//
// `progress` (optionnel, appelé depuis le thread d'import) reçoit une fraction dans [0, 1] et le nom de
// l'étape ; renvoyer false annule l'import (erreur kImportCancelled). La lecture du fichier elle-même
// (tinygltf) n'est pas interruptible : l'annulation prend effet à l'étape suivante.
using ProgressCallback = std::function<bool(float fraction, std::string_view stage)>;
inline constexpr const char* kImportCancelled = "Import cancelled";

Result<Character> importGltf(const std::filesystem::path& path, const ProgressCallback& progress = {});

}  // namespace rigger::io
