# Format de projet Rigger (`.rig`)

Un projet est **un seul fichier JSON UTF-8**. Les gros tampons binaires (sommets, indices, poids, textures) sont des
blocs *little-endian* encodés en **base64** dans le JSON. Le fichier est écrit de façon atomique (fichier temporaire
`.rig.tmp` puis renommage) : un échec d'écriture ne détruit jamais l'ancien fichier.

Implémentation : `src/io/project.{hpp,cpp}` (aucune dépendance à Qt).

## Versions et migration

| Champ | Rôle |
|---|---|
| `format` | toujours `"rigger-project"` |
| `version` | entier, **schéma courant : 1** |

- Une version **supérieure** à celle de l'application est refusée avec un message demandant de mettre à jour.
- Une version **inférieure** est migrée à la lecture (`parseProject`) par des étapes successives N -> N+1 ; il n'y a pour
  l'instant qu'une version, donc aucune migration.
- Les champs facultatifs absents prennent leur valeur par défaut.

## Structure (version 1)

```json
{
  "format": "rigger-project", "version": 1, "name": "Figure",
  "view": { "yaw": 0.78, "pitch": 0.52, "distance": 4.0, "target": [0,0.5,0], "orthographic": false,
            "mode": 2, "clipIndex": 0, "time": 0.0, "sourcePath": "figure.glb" },
  "skeleton": { "bones": [ { "id": 0, "name": "root", "parent": -1,
                             "bind": { "t": [x,y,z], "r": [w,x,y,z], "s": [x,y,z] },
                             "length": 0.2, "flags": 0, "color": [r,g,b] } ] },
  "images":  [ { "width": 64, "height": 64, "rgba": "<base64>" } ],
  "meshes":  [ { "vertexCount": 370, "vertices": "<base64>", "indices": "<base64>",
                 "submeshes": [ { "name": "", "firstIndex": 0, "indexCount": 36,
                                  "material": { "name": "m", "baseColor": [1,1,1], "metallic": 0, "roughness": 0.5, "image": -1 } } ],
                 "skin": "<base64>" } ],
  "clips":   [ { "name": "wave", "duration": 1.0, "fps": 30, "loop": true, "rootMotion": false, "rootBone": -1,
                 "markers": [ { "time": 0.5, "name": "hit" } ],
                 "tracks": [ { "bone": 3, "translation": TRACK, "rotation": TRACK, "scale": TRACK } ] } ],
  "poses":   [ { "name": "pose A", "bones": [ { "name": "arm", "transform": { "t": [..], "r": [..], "s": [..] } } ] } ],
  "ikControls": [ ... ],
  "deform": { "twists": [], "constraints": [], "springs": [], "colliders": [], "springsEnabled": true },
  "mix": { "mode": 0, "layers": [], "strips": [], "tree": { "parameterName": "speed", "parameter": 0, "syncTime": true, "entries": [] } },
  "warnings": [ "..." ]
}
```

### Squelette
Os listés **parents d'abord**. `id` est l'identifiant stable (jamais réutilisé) ; `parent` vaut `-1` pour une racine.
`flags` : bit 0 masqué, bit 1 verrouillé, bit 3 os de poids de blend shape (toujours avec le bit 2), bit 2 os de contrôle (cible / pole IK). Rotations en quaternion
`[w, x, y, z]`, repère main droite, Y haut, mètres.

### Maillages
- `vertices` : par sommet 11 `float32` : position (3), normale (3), tangente (3), uv0 (2).
- `indices` : `uint32` (triangles).
- `skin` (facultatif) : par sommet 32 octets : 4 `uint32` (identifiants d'os, `0xFFFFFFFF` = aucun) puis 4 `float32`
  (poids, somme 1, ≤ 4 influences).
- `morphs` (facultatif) : blend shapes `{ name, weightBone, deltas (base64 : 3 `float32` par sommet), drivers }`. Le poids de
  la forme est la translation X de l'os de poids `weightBone` (os de contrôle, drapeau 3) ; `drivers` : `{ bone, axis, angle0,
  angle1 }` (shape corrective pilotée par l'angle d'une articulation).
- `image` d'un matériau : indice dans `images`, ou `-1`. `rgba` : `width * height * 4` octets.

### Clips
`TRACK` = `{ "time": [..], "value": [..], "in": [..], "out": [..], "mode": [..] }` : temps croissants (secondes) ;
`value`, `in`, `out` aplatis (3 nombres par clé pour translation / échelle, 4 pour la rotation `w x y z`) ; `mode` :
0 constant, 1 linéaire, 2 Bézier (les tangentes sont des pentes par seconde, équivalentes à CUBICSPLINE de glTF).

### Contrôles IK
`name`, `kind` (0 chaîne, 1 look-at), `aimAxis`, `locks` (verrous de pied : `start`, `end`, `world`), `chain`
(`bones`, `effector`, `solver` 0 Two-Bone / 1 FABRIK / 2 CCD, `limits`, `maxIterations`, `tolerance`, `damping`,
`effectorOrientation`), `targetBone`, `poleBone`, `poleTwist`, `weight`, `enabled`. Un contrôle dont un os a disparu est
ignoré à la lecture.

### Déformations (`deform`, facultatif)
`twists` : `{ name, source, bones, factors, axis, enabled }` ; `constraints` : `{ name, kind (0 Copy Transform, 1 Limit,
2 Child Of, 3 Track To), bone, target, influence, enabled, copy[3], limit[3], locationMin/Max, rotationMin/Max,
scaleMin/Max, inverse (16 nombres, colonnes), aimAxis }` ; `springs` : `{ name, bones, stiffness, damping, gravity,
enabled }` ; `colliders` : `{ bone, offset, radius }` ; `springsEnabled`. Les références à des os absents sont ignorées.

### Mélange d'animations (`mix`, facultatif)
`mode` : 0 clip seul, 1 couches, 2 séquenceur NLA, 3 blend tree. `layers` : `name`, `clip` (indice dans `clips`), `weight`,
`mode` (0 Override, 1 Additif), `mask` (os racines, descendants inclus), `speed`, `timeOffset`, `loop`, `enabled`.
`strips` : `name`, `clip`, `track`, `start`, `clipStart`, `clipEnd` (-1 : fin du clip), `speed`, `repeat`, `blendIn`,
`blendOut`, `weight`, `mode`, `enabled`. `tree` : `parameterName`, `parameter`, `syncTime`, `entries` (`clip`, `threshold`).
Les références de clips hors limites sont refusées à la lecture. Un projet sans `mix` s'ouvre en mode « clip seul ».

## Validation à la lecture
Fichier absent, vide, tronqué ou corrompu, version inconnue, base64 invalide, tailles de tampons incohérentes, indices
hors limites, poids hors [0, 1] ou sur un os inexistant, pistes sur un os inexistant, valeurs non finies, maillage
invalide : **erreur lisible, jamais de crash** (testé par troncatures et altérations aléatoires).

## Autosave
Toutes les 2 minutes, si le projet est modifié, une copie est écrite dans `<dossier temporaire>/rigger_autosave/autosave.rig`
(avec `autosave.json` : chemin du projet d'origine). Elle est supprimée à l'enregistrement et à une sortie propre ; au
lancement suivant (après un plantage), l'application propose de la récupérer.

## Export glTF 2.0 / GLB
`src/io/gltf_export.{hpp,cpp}` : squelette (un nœud par os, os de contrôle IK exclus), maillages skinnés (POSITION,
NORMAL, TEXCOORD_0, JOINTS_0, WEIGHTS_0, indices `uint32`), matériaux PBR de base (+ texture de couleur), animations
échantillonnées (`LINEAR`) sur la plage et au FPS choisis, avec options **bake IK** et **bake des déformations** (os de twist, contraintes et spring bones simulés image par image). Les blend shapes sont exportées en `targets` POSITION (+ `weights` par défaut, `extras.targetNames`) et leur animation en canal `weights`. Le GLB produit est validé par le
glTF-Validator de Khronos dans la CI (aucune erreur ni avertissement).
