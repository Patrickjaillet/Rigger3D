// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include "core/character/character.hpp"

namespace rigger {

// Personnage d'exemple « Rigby » (projet « Premier pas » et tests) : humanoïde procédural fait de prismes,
// entièrement généré par le code (aucun asset externe) donc libre de droits (CC0 1.0). Squelette humanoïde de
// 19 os (bras en T), skinning automatique, deux clips en boucle de 30 images à 30 IPS : « Wave » (salut) et «
// Walk » (marche sur place).
Character buildExampleCharacter();

}  // namespace rigger
