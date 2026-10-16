// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "core/character/character.hpp"
#include "core/result.hpp"

namespace rigger::io {

// Options d'export glTF 2.0 / GLB.
struct GltfExportOptions {
    bool binary = true;  // .glb (tampons et textures embarqués) ; sinon .gltf + .bin
    // Maillages exportés (un booléen par maillage) ; vide = tous.
    std::vector<bool> includeMeshes;
    // Clips exportés (un booléen par clip) ; vide = tous. `clipNames` (même taille, facultatif) renomme les
    // clips.
    std::vector<bool> includeClips;
    std::vector<std::string> clipNames;
    // Plage d'images [firstFrame, lastFrame] (lastFrame < 0 : jusqu'à la fin du clip) et FPS
    // d'échantillonnage (0 : FPS du clip). Les animations sont échantillonnées à ce rythme (interpolation
    // LINEAR).
    int firstFrame = 0;
    int lastFrame = -1;
    float fps = 0.0f;
    // Applique la couche IK à chaque image : les rotations des os pilotés par l'IK sont exportées (« bake IK
    // »).
    bool bakeIK = true;
    // Applique aussi les os de twist et les contraintes, et simule les spring bones (cheveux, queue...) image
    // par image : le mouvement dynamique est exporté sous forme de clés de rotation (aucun moteur ne simule
    // nos spring bones).
    bool bakeDeform = true;
};

// Exporte le personnage : squelette (nœuds), maillages skinnés (POSITION / NORMAL / TEXCOORD_0 / JOINTS_0 /
// WEIGHTS_0), matériaux PBR de base et animations. Les os de contrôle IK (cible / pole) ne sont pas exportés.
Result<void> exportGltf(const Character& character, const std::filesystem::path& path,
                        const GltfExportOptions& options = {});

}  // namespace rigger::io
