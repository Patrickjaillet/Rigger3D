# Architecture de Rigger

## Modules et règles de dépendances

```
            ┌────────┐
            │  app   │  main(), CLI, crash handler, journal fichier
            └───┬────┘
                │
            ┌───▼────┐
            │ editor │  Qt 6 Widgets : Document, panneaux, MainWindow, thème, i18n
            └─┬──┬─┬─┘
      ┌───────┘  │ └────────┐
 ┌────▼───┐  ┌───▼───┐  ┌───▼────┐
 │ render │  │  io   │  │        │
 │ OpenGL │  │ .rig  │  │        │
 │ (glad) │  │ glTF  │  │        │
 └────┬───┘  └───┬───┘  │        │
      └────┬─────┘      │        │
        ┌──▼───┐        │        │
        │ core │◄───────┘        │
        └──────┘  GLM + STL seulement
```

| Module | Contenu | Peut dépendre de | Interdit |
|---|---|---|---|
| `src/core` | maths, squelette, pose / FK, maillage, skinning, IK, animation (clips, mix, retarget), déformations (blend shapes, twist, contraintes, springs, auto-rig), commandes + historique, personnage d'exemple | GLM, STL | Qt, OpenGL, fichiers, réseau |
| `src/io` | projet `.rig`, import / export glTF 2.0 / GLB (tinygltf) | `core` | Qt, OpenGL, réseau |
| `src/render` | rendu OpenGL 4.1 (glad) : maillage, skinning GPU, squelette, gizmos, grille, caméra | `core` | Qt, réseau |
| `src/editor` | widgets Qt, `Document`, panneaux, actions, thème, traductions | `core`, `io`, `render`, Qt | réseau |
| `src/app` | `main()`, ligne de commande, rapports de plantage | tout | réseau |

Ces règles sont **vérifiées automatiquement** par `tools/check_deps.py` (test CTest `check_deps`, étape de CI) : une
inclusion Qt dans `core/`, `io/` ou `render/`, une inclusion OpenGL hors de `render/`, tinygltf hors de `io/`, ou toute
bibliothèque réseau (aucune télémétrie) fait échouer la build.

## Conventions du `core`

- `BoneId` est stable et jamais réutilisé ; c'est aussi l'indice dans `Pose` et dans les tableaux de `computeGlobals`.
  On itère avec `skeleton.order()` (parents d'abord).
- Repère main droite, Y haut, mètres, radians ; rotations en quaternions (Euler = affichage uniquement).
- Poses = TRS locaux ; calcul monde en `glm::mat4` ; `global = parentGlobal * local`, `skin = global * inverseBind`.
- Erreurs d'I/O et d'API : `Result<T>`, pas d'exceptions dans les boucles temps réel.

## Pipeline de pose

`Document::pose()` enchaîne, à chaque image :

1. **FK / clip** (ou mélange de couches : `core/animation/mix`) ;
2. **IK** (Two-Bone, FABRIK, CCD) ; cibles et pôles sont des *os de contrôle*, animés par le système de clés normal ;
3. **twist** et **contraintes** (Copy, Limit, Track-To, Child-Of) ;
4. **spring bones** (simulation à état, déterministe) ;
5. **skinning** LBS / dual quaternion (CPU parallélisé ou GPU) ; les blend shapes sont appliqués aux sommets au repos
   avant le skinning.

## Édition : documents, commandes, historique

- Toute modification passe par une `Command` du `core` (`do` / `undo`) ; `History` les empile et fusionne les gestes
  continus (un glissé = une entrée). Un miroir `QUndoStack` alimente les actions Annuler / Rétablir.
- Les `Effect` (Transforms, Properties, Structure, Weights, Animation, IK, Deform) indiquent aux panneaux ce qu'ils
  doivent rafraîchir.
- `editor/Document` (QObject) détient le personnage, la sélection, le mode / l'outil, le clip et le temps ; les panneaux
  n'écrivent jamais dans le personnage sans passer par lui.

## Formats

- **`.rig`** : JSON UTF-8, tampons binaires en base64, `version` de schéma, migrations, écriture atomique, lecture
  défensive (fichiers tronqués ou altérés → erreur lisible, jamais de crash ; fuzzing `fuzz/`).
- **glTF 2.0 / GLB** : import (maillage, skin, clips, morph targets) et export (avec *bake* de l'IK et des déformations).
  Les GLB exportés passent le glTF-Validator sans erreur.

## Qualité

- Tests : doctest (`core`, `io`, `render`) et Qt Test (`editor`, plateforme offscreen), tests GPU, budgets de
  performance (`rigger_perf`), test de plantage contrôlé, fuzzing libFuzzer, snapshot du rendu.
- CI (`.github/workflows/ci.yml`) : matrice 3 OS, sanitizers, format, validation glTF, couverture, fuzzing.
- Release (`.github/workflows/release.yml`) : sur tag `v*`, binaires des 3 OS via CPack.
