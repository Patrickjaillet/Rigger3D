# Rigger3D

Logiciel d'animation 3D de personnages (squelette, skinning, FK/IK, timeline) en **C++20 + Qt 6**.
Version **1.0.0** ([CHANGELOG.md](CHANGELOG.md)). Feuille de route détaillée : [ROADMAP.md](ROADMAP.md).

## Licence et contact
© 2026 Patrick JAILLET — distribué sous licence **GPL-3.0-or-later** (voir [LICENSE](LICENSE) et [COPYING](COPYING)).
Composants tiers et compatibilité : [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Contact : sandefjord.development@proton.me · Site : https://patrickjaillet.github.io/Rigger3D

## Prise en main
Lancez Rigger, cliquez sur **Ouvrir l'exemple** (personnage « Rigby », libre de droits, avec une visite guidée), puis suivez le
[guide utilisateur](docs/user_guide.md) : importer → peindre → poser → animer → exporter. Architecture et règles de modules :
[docs/architecture.md](docs/architecture.md). Aucune télémétrie : Rigger n'envoie aucune donnée et n'utilise pas le réseau.

## Prérequis
- CMake ≥ 3.25, Ninja, un compilateur C++20 (GCC 12+, Clang 15+, MSVC 2022)
- **Qt 6.4+** (6.8 LTS recommandé), modules Core/Gui/Widgets/OpenGL/OpenGLWidgets/Svg/Concurrent/LinguistTools/Test
- Python 3 (script de vérification des dépendances)
- Accès réseau à GitHub à la première configuration (GLM, spdlog, nlohmann/json, glad, tinygltf, doctest et Qt Advanced Docking System sont téléchargés)

### Installer Qt
| OS | Commande |
|---|---|
| Ubuntu | `sudo apt install qt6-base-dev libgl1-mesa-dev ninja-build cmake` |
| macOS | `brew install qt ninja cmake` |
| Windows | Installeur Qt (MSVC 2019/2022 64 bits) ou `pip install aqtinstall` puis `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64` |

Si CMake ne trouve pas Qt : `-DCMAKE_PREFIX_PATH=/chemin/vers/Qt/6.x/<kit>`.

## Fonctionnalités (état actuel)
- **Import** glTF / GLB (squelette, maillages skinnés, animations), avec progression et annulation.
- **Modes** `Rig` (1), `Skin` (2), `Animate` (3) ; sélection, gizmos déplacer / tourner / étirer (`Q W E R`), symétrie X, masquage / verrouillage / isolation d'os.
- **Annuler / rétablir** illimité (200 par défaut) pour toute édition, panneau Historique.
- **Animation** : timeline (scrub, zoom, sélection / déplacement / duplication / étirement des clés, copier-coller, repères, plage de travail), **clé auto**, `K` pour insérer des clés, éditeur de **courbes** (poignées Bézier), **pelure d'oignon**, bibliothèque de **poses**, plusieurs **clips**, lecture (boucle, aller-retour, vitesse).
- **IK** : Two-Bone (avec pole), FABRIK, CCD, limites d'articulation, look-at, verrous de pied, bascule IK/FK (`I`), **bake IK -> FK**, préréglage humanoïde. Cibles et poles = os de contrôle, donc animables comme n'importe quel os.
- **Weight painting** (mode Skin) : pinceau (ajouter / soustraire / remplacer / lisser, falloff, symétrie X, sommets visibles), heatmap de l'os actif, normalisation automatique, verrouillage d'os, sélection de sommets + poids exact, **skinning automatique** (distance ou diffusion de chaleur), sélection de sommets (clic, rectangle, lasso), outils (normaliser, limiter à 4, élaguer, copier les poids), test de pose en direct.
- **Mélange d'animations** : couches avec masque d'os (Override / Additif), séquenceur NLA avec fondus enchaînés, blend tree 1D (marche <-> course), **rétro-ciblage** d'un clip entre deux personnages (correspondance automatique des os humanoïdes).
- **Déformations avancées** : blend shapes (import / export glTF, sliders, animables, shapes correctives pilotées par les articulations), os de twist, contraintes (Copy Transform, Limit, Child-Of, Track-To), **spring bones** (cheveux, queue ; collisions sphères, bake en clés), **auto-rig humanoïde** (repères -> squelette + skinning + IK).
- **Projets et export** : format `.rig` (voir [docs/file_format.md](docs/file_format.md)), ouvrir / enregistrer, **autosave** toutes les 2 min avec récupération après plantage, **export glTF / GLB** (plage, FPS, maillages, clips, bake IK), export PNG, séquence PNG transparente et MP4 (ffmpeg).
- **Qualité** : profileur + panneau Statistiques (CPU / GPU), budgets de performance testés, fuzzing, snapshot visuel, rapport de crash local avec minidump, contrastes WCAG AA, taille de police réglable.
- **Interface** : docking (Qt Advanced Docking System), thèmes clair / sombre, français / anglais à chaud, raccourcis configurables.

## Raccourcis principaux
| Touche | Action | Touche | Action |
|---|---|---|---|
| `1` `2` `3` | Rig / Skin / Animate | `Espace` | Lecture / pause |
| `Q` `W` `E` `R` | Sélection / déplacer / tourner / étirer | `←` `→` | Image précédente / suivante |
| `K` | Insérer une clé | `Maj+←` `Maj+→` | Première / dernière image |
| `Ctrl+K` | Clé auto | `Maj+O` | Pelure d'oignon |
| `Ctrl+O` `Ctrl+S` | Ouvrir / enregistrer le projet | `Ctrl+Maj+S` `Ctrl+Maj+E` | Enregistrer sous / exporter glTF |
| `I` | Bascule IK / FK | `Ctrl+Z` `Ctrl+Y` | Annuler / rétablir |
| `F` | Cadrer la sélection | `Suppr` | Supprimer (os en mode Rig, clés dans la timeline) |
| `[` `]` | Rayon du pinceau (mode Skin) | `Alt`+glisser | Orbiter la caméra |

Dans la timeline : clic / rectangle = sélectionner, glisser = déplacer, `Alt`+glisser = dupliquer, `Ctrl`+glisser = étirer, `Maj`+glisser = décaler toutes les clés, molette = zoom, clic milieu = panoramique.

## Compiler et tester
```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/src/app/rigger          # lance l'application
./build/debug/src/app/rigger modele.glb              # ouvre directement un glTF / GLB
./build/debug/src/app/rigger --screenshot-window out.png modele.glb   # capture de la fenêtre (CI)
./build/debug/src/app/rigger modele.glb --mode animate --humanoid-ik --screenshot-window ik.png   # IK humanoïde
```
Autres options : `--example` (ouvre l'exemple), `--make-example fichier.rig` (régénère `assets/examples/first_steps.rig`), `--export-gltf out.glb`, `--export-frames dossier`, `--smoke-test` (CI), `--screenshot`, `--select os1,os2`, `--tool select|move|rotate|scale`, `--mode rig|skin|animate`.
Presets : `debug`, `release`, `asan` (Linux/macOS), `core-only` (sans Qt), `coverage` (Linux/macOS, gcovr).
Fuzzing : `cmake -DRIGGER_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++ ...` puis `fuzz_gltf` / `fuzz_project` (libFuzzer). Budgets de performance : `ctest -R rigger_perf` (imposés en Release).

## Paquets et installation
```bash
cmake --preset release && cmake --build --preset release
cd build/release && cpack            # ZIP (+ NSIS si makensis est installé) sous Windows ; TGZ + DEB sous Linux ; DMG sous macOS
cmake --install build/release --component Rigger --prefix /chemin/installation   # installation directe
```
L'installation déploie les bibliothèques Qt (liaison **dynamique**, Qt est utilisé sous licence LGPL v3), le guide, l'exemple et
`THIRD_PARTY_LICENSES.txt` (régénéré par `python3 tools/gen_third_party.py`). `CHANGELOG.md` est généré par
`python3 tools/gen_changelog.py`. Conformité de licence (en-tête SPDX dans chaque fichier source, `LICENSE`, `COPYING`) : `python3 tools/license_headers.py --root .` (`--fix` ajoute les en-têtes manquants ; test CTest `check_license`). La publication automatique par tag (`v1.0.0`) est décrite dans `.github/workflows/release.yml`.
État de vérification : le ZIP et l'installeur NSIS Windows sont construits et testés localement (installation, lancement, désinstallation) ; DEB, DMG et le workflow de release n'ont pas encore été exécutés.

Traductions : les textes de l'interface sont en anglais dans les sources ; `cmake --build --preset debug --target update_translations` régénère `assets/ui/i18n/*.ts`, puis `python3 tools/fill_fr_translations.py` complète le français.

## Structure
```
src/core     maths, squelette, animation, IK, skinning, commandes annulables (sans Qt, sans OpenGL)
src/io       import/export (glTF)
src/render   rendu OpenGL (sans Qt)
src/editor   interface Qt (fenêtres, panneaux)
src/app      point d'entrée
tests/       doctest (core/io/render) + Qt Test (editor)
tools/       check_deps.py (règles d'architecture), générateurs de données de test, d'icônes, de licences et de changelog
docs/        guide utilisateur, architecture, format de fichier
assets/      ui (thème QSS, police Inter, icônes SVG, traductions), app (icône), examples (projet « Premier pas »), LICENSES.md
```
