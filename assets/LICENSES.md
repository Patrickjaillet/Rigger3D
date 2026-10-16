# Licences des contenus livrés (`assets/`)

| Contenu | Origine | Licence |
|---|---|---|
| `examples/first_steps.rig` — personnage « Rigby » et clips *Wave* / *Walk* | **Généré par le code** (`src/core/example/example.cpp`, commande `rigger --make-example`) : aucune donnée externe, aucun modèle tiers | **CC0 1.0** (domaine public) |
| `app/rigger.svg`, `rigger-*.png`, `rigger.ico` | Générés par `tools/make_app_icon.py` | CC0 1.0 |
| `ui/icons/*.svg` | Générés par `tools/make_ui_icons.py` | CC0 1.0 |
| `ui/fonts/Inter*` | Rasmus Andersson, Inter | SIL Open Font License 1.1 (voir `THIRD_PARTY_LICENSES.txt`) |
| `ui/theme.qss`, `ui/i18n/*.ts` | Écrits pour Rigger3D | GPL-3.0-or-later (licence du projet) |

## Ce qui n'est PAS livré

Les modèles de test `RiggedFigure` et `CesiumMan` (Khronos glTF-Sample-Models) sont sous **CC-BY 4.0** (attribution
obligatoire) et non CC0 : ils ne servent qu'aux tests (`tests/data/`, non installés) et ne doivent pas être redistribués
comme « exemples CC0 ». Voir `tests/data/README.md`.

Les bibliothèques tierces (GLM, spdlog, nlohmann_json, glad, tinygltf, doctest, Qt Advanced Docking System, Qt) et leurs
licences sont listées dans `THIRD_PARTY_LICENSES.txt` (généré par `tools/gen_third_party.py`).
