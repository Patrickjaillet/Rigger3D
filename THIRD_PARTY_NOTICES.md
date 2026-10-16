<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 Patrick JAILLET -->

# Notices des composants tiers — compatibilité avec la GPL-3.0-or-later

Rigger3D est distribué sous licence **GPL-3.0-or-later** (voir `LICENSE` et `COPYING`). Ce document consigne, pour chaque
composant tiers, sa licence et la raison pour laquelle il peut être combiné avec un programme sous GPL-3.0. Le texte
intégral de chaque licence est reproduit dans `THIRD_PARTY_LICENSES.txt` (généré par `tools/gen_third_party.py`, aussi
affiché dans `Aide > À propos > Third-Party Licenses...`).

| Composant | Version | Rôle | Licence | Compatible GPL-3.0 | Distribué |
|---|---|---|---|---|---|
| GLM | 1.0.1 | mathématiques 3D | MIT | Oui (permissive) | Oui (header-only, compilé dans l'exécutable) |
| spdlog | 1.14 | journalisation | MIT | Oui | Oui (compilé dans l'exécutable) |
| nlohmann/json | 3.11 | JSON des projets `.rig` | MIT | Oui | Oui (compilé dans l'exécutable) |
| glad | 2.0.8 | chargeur OpenGL | MIT (code généré : domaine public) | Oui | Oui (compilé dans l'exécutable) |
| TinyGLTF (+ stb_image, stb_image_write) | 2.9.3 | import / export glTF | MIT (stb : domaine public ou MIT) | Oui | Oui (compilé dans l'exécutable) |
| Qt Advanced Docking System | 4.4.1 | panneaux ancrables | LGPL-2.1 | Oui : l'article 3 de la LGPL-2.1 autorise la conversion de toute copie en GPL (version 2 ou ultérieure), donc en GPL-3.0 | Oui (lié statiquement) |
| Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets, Svg, Concurrent) | 6.8 | interface | LGPL-3.0 | Oui : la GPL-3.0 et la LGPL-3.0 se combinent explicitement (art. 3 de la LGPL-3.0 / art. 13 de la GPL-3.0) | Oui (**liaison dynamique**, bibliothèques remplaçables) |
| Inter (police) | The Inter Project Authors | texte de l'interface | SIL OFL 1.1 | Oui (police embarquée comme donnée ; la licence OFL accompagne la police) | Oui (ressource Qt) |
| doctest | 2.4.11 | tests unitaires | MIT | Oui | **Non** (outil de développement) |

## Remarques

- **Qt** : utilisé uniquement par liaison dynamique ; les bibliothèques Qt livrées à côté de l'exécutable peuvent être
  remplacées par une version modifiée compatible. Le code source de Qt est disponible sur <https://download.qt.io>. Les
  bibliothèques Qt embarquent elles-mêmes des composants tiers (par exemple libpng, FreeType, HarfBuzz) régis par leurs
  propres licences, décrites dans la documentation de Qt (« Licenses Used in Qt »).
- **Contenus** : l'icône, les icônes d'interface et le personnage d'exemple sont générés par des scripts ou du code du
  projet ; voir `assets/LICENSES.md`. Les modèles de test `RiggedFigure` et `CesiumMan` (Khronos, CC-BY 4.0) ne servent
  qu'aux tests, ne sont pas installés et ne sont pas redistribués avec l'application.
- **Aucune bibliothèque réseau** n'est utilisée par le code de Rigger3D (vérifié par `tools/check_deps.py`) : aucune
  télémétrie.
- **Vérification** : cette liste est établie à partir des fichiers de licence des sources téléchargées lors de la
  configuration CMake. À chaque ajout ou mise à jour d'une dépendance, mettre à jour `tools/gen_third_party.py`, régénérer
  `THIRD_PARTY_LICENSES.txt` et ce document.
