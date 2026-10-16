// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "core/animation/keys.hpp"
#include "core/animation/mix.hpp"
#include "core/animation/player.hpp"
#include "core/animation/retarget.hpp"
#include "core/character/character.hpp"
#include "core/command/commands.hpp"
#include "core/command/history.hpp"
#include "core/deform/autorig.hpp"
#include "core/deform/deform.hpp"
#include "core/math/ray.hpp"
#include "core/result.hpp"
#include "core/skeleton/manipulate.hpp"
#include "core/skeleton/pose.hpp"
#include "core/skinning/paint.hpp"

class QUndoStack;

namespace rigger::editor {

class UndoAdapter;

enum class Mode { Rig, Skin, Animate };
enum class Tool { Select, Move, Rotate, Scale };
enum class SkinMethod { Lbs, Dqs };
enum class VertexSelectShape { Rectangle, Lasso };

enum class BoneKeyState : std::uint8_t {
    None,
    Keyed,
    Modified
};  // clé à la frame courante / modifié non keyé
enum class KeyDragMode { Move, Duplicate, Scale };

struct OnionSkin {
    bool enabled = false;
    int before = 2;  // nombre de fantômes avant / après
    int after = 2;
    int step = 1;  // écart en images entre deux fantômes
};

// Modification d'une clé depuis l'éditeur de courbes. Rotation : `value` est en angles d'Euler XYZ (radians).
struct KeyEdit {
    std::optional<double> time;
    std::optional<glm::vec3> value;
    std::optional<glm::vec3> inTangent;  // translation / échelle uniquement
    std::optional<glm::vec3> outTangent;
};

// État partagé de l'éditeur : personnage courant, sélection, mode/outil, temps de lecture, pose évaluée.
// Les panneaux sont de simples vues qui écoutent les signaux. Toutes les éditions passent par les méthodes
// ci-dessous (point d'entrée unique que la Phase 8 remplacera par des commandes annulables).
//
// Pose affichée : bind pose en modes Rig et Skin ; en mode Animate, le clip courant échantillonné au temps
// courant (ou la bind pose s'il n'y a pas de clip), puis les retouches manuelles jusqu'au prochain
// changement de temps.
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    // --- Personnage -----------------------------------------------------------------------
    // Remplace le personnage (nullptr : document vide). Réinitialise sélection, temps et pose ; mode Animate,
    // lecture automatique si un clip existe.
    void setCharacter(std::shared_ptr<Character> character, const QString& sourcePath = {});
    std::shared_ptr<const Character> character() const { return character_; }
    bool hasCharacter() const { return character_ != nullptr; }
    const QString& sourcePath() const { return sourcePath_; }
    const Skeleton* skeleton() const { return character_ ? &character_->skeleton : nullptr; }
    // Pose AFFICHÉE : pose FK (clip + retouches) avec la couche IK par-dessus en mode Animate.
    const Pose& pose() const;
    const Pose& fkPose() const { return pose_; }
    std::size_t vertexCount() const;

    // --- Sélection ------------------------------------------------------------------------
    const std::vector<BoneId>& selection() const { return selection_; }
    bool isSelected(BoneId id) const;
    void setSelection(std::vector<BoneId> ids);  // ignore les os inconnus, supprime les doublons
    void clearSelection() { setSelection({}); }

    // --- Mode / outil ---------------------------------------------------------------------
    Mode mode() const { return mode_; }
    void setMode(Mode mode);
    Tool tool() const { return tool_; }
    void setTool(Tool tool);

    // --- Temps ----------------------------------------------------------------------------
    int clipIndex() const { return clipIndex_; }  // -1 : aucun clip
    const AnimationClip* clip() const;
    void setClipIndex(int index);
    double fps() const;
    void setDefaultFps(double fps);
    double time() const { return time_; }
    int frame() const;
    int frameCount() const;
    void setTime(double seconds);
    void setFrame(int frame);
    bool isPlaying() const { return playing_; }
    void setPlaying(bool playing);
    void togglePlaying() { setPlaying(!playing_); }

    // --- Skinning -------------------------------------------------------------------------
    SkinMethod skinMethod() const { return skinMethod_; }
    void setSkinMethod(SkinMethod method);

    // --- Éditions (os) --------------------------------------------------------------------
    Result<void> renameBone(BoneId id, const QString& name);
    // keepWorld : conserve la position monde de bind de l'os.
    Result<void> reparentBone(BoneId id, BoneId newParent, bool keepWorld = true);
    // Transformation locale éditée : bind local en mode Rig, pose courante sinon.
    Transform boneLocal(BoneId id) const;
    bool setBoneLocal(BoneId id, const Transform& local);  // false si l'os est verrouillé ou inconnu
    bool setBoneLength(BoneId id, float length);
    bool setBoneColor(BoneId id, const glm::vec3& color);
    bool setBoneFlags(BoneId id, BoneFlags flags);  // le verrouillage lui-même reste toujours modifiable

    // --- Annuler / rétablir -------------------------------------------------------------------
    // Toutes les éditions ci-dessous passent par des commandes (core/command) : une action = une entrée
    // d'historique, un glissé de gizmo ou de curseur = une seule entrée. Les retouches de pose des modes Skin
    // / Animate sont annulables mais restent transitoires (elles deviendront des clés en Phase 9).
    bool canUndo() const { return history_.canUndo() && !manipulating_; }
    bool canRedo() const { return history_.canRedo() && !manipulating_; }
    void undo();
    void redo();
    QString undoText() const;  // « Move Bones (elbow) » traduit ; vide si rien à annuler
    QString redoText() const;
    bool isClean() const {
        return history_.isClean() && !forcedDirty_;
    }  // faux : modifications non sauvegardées
    void markClean() {
        history_.markClean();
        forcedDirty_ = false;
        emit historyChanged();
    }
    // Marque le document comme modifié sans toucher à l'historique (projet récupéré après un crash).
    void markDirty() {
        forcedDirty_ = true;
        emit historyChanged();
    }
    // Termine une série d'éditions continues (curseur relâché) : la suivante ne fusionnera pas avec elle.
    void endContinuousEdit() { history_.breakMerge(); }
    History& history() { return history_; }
    const History& history() const { return history_; }
    QUndoStack* undoStack() const;

    // Éditions groupées (un seul historique par appel) : transformations locales et propriétés de plusieurs
    // os. `mergeKey` non vide : les appels consécutifs de même clé fusionnent (curseur d'inspecteur).
    bool setBonesLocal(const std::vector<std::pair<BoneId, Transform>>& edits,
                       const std::string& mergeKey = {});
    bool setBonesProperties(const std::vector<BoneId>& ids, const std::function<void(BoneProps&)>& modify,
                            const std::string& name, const std::string& mergeKey = {},
                            bool ignoreLock = false);

    // --- Animation : lecture, clés, clips, poses (Phase 9) --------------------------------------
    double playbackSpeed() const { return player_.speed(); }
    void setPlaybackSpeed(double speed);
    bool loopPlayback() const { return player_.loop(); }
    void setLoopPlayback(bool loop);
    bool pingPong() const { return player_.pingPong(); }
    void setPingPong(bool pingPong);
    void setPlaybackRange(double start, double end);  // end < 0 : fin du clip
    double rangeStart() const { return player_.rangeStart(); }
    double rangeEnd() const { return player_.rangeEnd(); }
    bool hasPlaybackRange() const { return player_.hasCustomRange(); }

    bool autoKey() const { return autoKey_; }
    void setAutoKey(bool enabled);
    // État de chaque os (indexé par BoneId) à la frame courante : clé présente, modifié non keyé, ou rien.
    std::vector<BoneKeyState> boneKeyStates() const;

    // Sélection de clés (partagée par la timeline et l'éditeur de courbes ; non annulable).
    const std::vector<KeyRef>& keySelection() const { return keySelection_; }
    void setKeySelection(std::vector<KeyRef> keys);

    // Insère des clés (canaux du masque) pour les os, à la frame courante, avec leur pose actuelle (touche
    // K).
    bool insertKeys(const std::vector<BoneId>& bones, unsigned channelMask = kAllChannels);
    bool insertKeysAt(const std::vector<BoneId>& bones, unsigned channelMask,
                      double seconds);  // temps explicite
    std::size_t deleteKeys(const std::vector<KeyRef>& keys);
    bool setKeysInterpolation(const std::vector<KeyRef>& keys, Interpolation mode);
    bool editKey(const KeyRef& key, const KeyEdit& edit, bool merge = false);
    // Glissé de clés (déplacer / dupliquer / étirer) : begin capture, update applique en direct (secondes de
    // décalage, ou facteur d'échelle autour de `pivot`), end enregistre UNE commande. Décalage global :
    // toutes les clés.
    bool beginKeyDrag(const std::vector<KeyRef>& keys, KeyDragMode mode, double pivot = 0.0);
    void updateKeyDrag(double value);
    void endKeyDrag();
    bool isDraggingKeys() const { return keyDrag_.active; }
    void copyKeys(const std::vector<KeyRef>& keys);
    bool hasKeyClipboard() const { return !keyClipboard_.empty(); }
    std::vector<KeyRef> pasteKeys(double time);

    Result<void> newClip();
    Result<void> duplicateClip();
    Result<void> renameClip(const QString& name);
    Result<void> deleteClip();
    bool setClipFps(float fps);
    bool setClipLoop(bool loop);
    bool setClipDuration(double seconds);
    bool addMarker(double time, const QString& name);
    bool removeMarker(std::size_t index);

    Result<void> savePose(const QString& name, bool selectedOnly);
    bool applyPose(std::size_t index);
    bool deletePose(std::size_t index);

    const OnionSkin& onionSkin() const { return onion_; }
    void setOnionSkin(const OnionSkin& onion);

    // --- Déformations avancées (Phase 14) ---------------------------------------------------------
    const DeformRig& deform() const;
    // Modifie la configuration (twist, contraintes, springs, colliders) : une commande annulable.
    bool editDeform(const std::function<void(DeformRig&)>& modify, const char* name = "Edit Deform",
                    const std::string& mergeKey = {});
    // Crée `count` os de twist entre `forearm` et son enfant `hand` + la chaîne qui répartit la torsion de
    // `hand`.
    Result<int> createTwistSetup(BoneId forearm, BoneId hand, int count);
    // Chaîne de spring bones : `root` puis ses premiers enfants successifs (boneCount os).
    Result<int> createSpringChain(BoneId root, int boneCount);
    bool addSpringCollider(BoneId bone, const glm::vec3& offset, float radius);
    Result<int> addConstraint(Constraint constraint);  // ChildOf : l'inverse de la cible est capturé ici
    // Blend shapes : poids (translation X de l'os de poids, avec auto-key) et pilotes correctifs.
    bool setMorphWeight(std::size_t mesh, std::size_t target, float value);
    bool editMorphDrivers(std::size_t mesh, std::size_t target, std::vector<MorphDriver> drivers);
    // Simulation des spring bones : avancée par la lecture ; réinitialisée quand on change de temps.
    bool springsActive() const;
    // Fige la simulation en clés de rotation FK sur [firstFrame, lastFrame] (une entrée d'historique).
    Result<void> bakeSprings(int firstFrame, int lastFrame, bool disableAfter);
    // Auto-rig humanoïde : remplace le squelette par un squelette généré depuis les repères, skinne tous les
    // maillages et (option) crée l'IK humanoïde. Les clips, l'IK, le mélange et les déformations existants
    // sont supprimés.
    Result<void> autoRigHumanoid(const Landmarks& landmarks, const AutoSkinOptions& skinning = {},
                                 bool withIK = true);
    // Repères proposés d'après les sommets du personnage (boîte englobante).
    Landmarks guessCharacterLandmarks() const;

    // --- Mélange d'animations (Phase 13) ---------------------------------------------------------
    const AnimationMix& mix() const;
    bool mixActive() const;  // mode Animate + mélange (couches / NLA / blend tree) + au moins un clip
    double timelineDuration() const;  // durée de la timeline : clip courant, ou durée du mélange
    // Modifie le mélange (couches, bandes, blend tree, mode) : une commande annulable ; mergeKey fusionne les
    // curseurs.
    bool editMix(const std::function<void(AnimationMix&)>& modify, const char* name = "Edit Mix",
                 const std::string& mergeKey = {});
    // Fondu enchaîné A -> B : deux bandes NLA qui se recouvrent sur `duration` secondes ; passe en mode NLA.
    bool addCrossfade(int clipA, int clipB, double duration);
    // Rétro-cible un clip d'un autre personnage sur ce squelette (nouveau clip, annulable) ; retourne son
    // indice.
    Result<int> retargetClipFrom(const Character& source, std::size_t clipIndex, const BoneMap& map,
                                 const RetargetOptions& options = {});

    // --- IK (Phase 10) ----------------------------------------------------------------------
    // Les cibles et poles sont des os de contrôle (BoneFlags::Control) : leurs clés passent par le système
    // normal.
    const std::vector<IKControl>& ikControls() const;
    // Crée un contrôle IK sur `boneCount` os au-dessus de `effector` : os cible (à la position de
    // l'effecteur) et, si demandé (TwoBone), os pole décalé de `poleDistance` mètres dans `poleDirection`.
    // Annulable en une entrée.
    Result<int> createIK(BoneId effector, int boneCount, IKSolver solver, bool withPole,
                         const glm::vec3& poleDirection = {0.0f, 0.0f, 1.0f}, float poleDistance = 0.5f);
    // Look-at : l'axe local +Y de `bone` vise un os cible créé à `distance` mètres devant lui (+Z).
    Result<int> createLookAt(BoneId bone, float distance = 1.0f);
    // Modifie un contrôle (réglages) ; mergeKey non vide : fusionne les modifications continues.
    bool editIK(int index, const std::function<void(IKControl&)>& modify, const char* name = "Edit IK",
                const std::string& mergeKey = {});
    bool removeIK(int index);  // retire le contrôle (les os de contrôle restent)
    // Bascule IK <-> FK : le poids passe en douceur à 0 ou 1 (0,25 s) ; `animated` = false : immédiat.
    void toggleIK(int index, bool animated = true);
    // Contrôle IK concernant cet os (cible, pole, os de la chaîne ou effecteur) ; -1 si aucun.
    int ikControlFor(BoneId bone) const;
    bool isIKBlending() const { return ikBlend_.active; }
    // Convertit l'IK en clés de rotation FK sur [firstFrame, lastFrame], puis désactive éventuellement le
    // contrôle.
    Result<void> bakeIK(int index, int firstFrame, int lastFrame, bool disableAfter);
    // Verrou de pied : la cible reste figée à sa position (au début du verrou) entre les deux images.
    Result<void> addFootLock(int index, int firstFrame, int lastFrame);
    bool removeFootLock(int index, int lockIndex);
    // Préréglage : bras, jambes (TwoBone), colonne (FABRIK) et tête (look-at) trouvés par nom d'os.
    Result<int> createHumanoidIK();

    // --- Skin : weight painting (Phase 11) ----------------------------------------------------
    struct VertexRef {
        std::size_t mesh = 0;
        std::uint32_t vertex = 0;
        bool operator==(const VertexRef&) const = default;
    };
    const Brush& brush() const { return brush_; }
    void setBrush(const Brush& brush);
    bool autoNormalizeWeights() const { return autoNormalize_; }
    void setAutoNormalizeWeights(bool enabled);
    bool paintOnlyVisible() const { return paintVisibleOnly_; }
    void setPaintOnlyVisible(bool enabled);
    bool paintActive() const { return paintActive_; }  // en mode Skin, le pinceau remplace le picking d'os
    void setPaintActive(bool active);
    bool vertexSelectMode() const { return vertexSelectMode_; }  // clic / rectangle : sélection de sommets
    void setVertexSelectMode(bool enabled);
    VertexSelectShape vertexSelectShape() const { return vertexSelectShape_; }
    void setVertexSelectShape(VertexSelectShape shape);
    BoneId activeBone() const;  // premier os sélectionné qui n'est pas un os de contrôle
    bool isWeightLocked(BoneId id) const { return weightLocks_.isLocked(id); }
    void setWeightLocked(BoneId id, bool locked);
    const WeightLocks& weightLocks() const { return weightLocks_; }
    // Positions / normales des sommets dans la pose affichée (CPU, LBS).
    std::vector<Vertex> skinnedVertices(std::size_t mesh) const;
    std::optional<glm::vec3> raycastSurface(const Ray& ray) const;  // premier point du maillage touché
    // Coup de pinceau : begin / paintAt (autant que nécessaire) / end = UNE commande annulable (delta des
    // sommets).
    bool beginPaintStroke();
    void paintAt(const glm::vec3& center, const glm::vec3& eye);
    void endPaintStroke();
    bool isPainting() const { return strokeActive_; }
    // Sélection de sommets + poids exact de l'os actif sur la sélection.
    const std::vector<VertexRef>& vertexSelection() const { return vertexSelection_; }
    void setVertexSelection(std::vector<VertexRef> vertices);
    bool setSelectedVertexWeight(float value);
    // Outils globaux (chacun = une commande annulable) ; retournent le nombre de sommets modifiés.
    Result<std::size_t> autoSkin(const AutoSkinOptions& options = {});
    Result<std::size_t> normalizeSkinWeights();
    Result<std::size_t> limitSkinInfluences(std::size_t maxInfluences = 4);
    Result<std::size_t> pruneSkinWeights(float threshold = 0.01f);
    Result<std::size_t> copySkinWeights(std::size_t fromMesh, std::size_t toMesh);

    // --- Sélection avancée, visibilité, isolation ----------------------------------------
    // Un os est « visible » s'il n'est pas masqué et, en mode isolation, appartient à l'ensemble isolé ; il
    // est « sélectionnable au picking » s'il est de plus déverrouillé.
    bool isBoneVisible(BoneId id) const;
    bool isBonePickable(BoneId id) const;
    void selectAll();                   // tous les os sélectionnables
    void selectRangeTo(BoneId target);  // plage dans l'ordre hiérarchique, de l'ancre de sélection à `target`
    void toggleInSelection(BoneId id);
    void hideSelected();
    void showAll();
    void lockSelected();
    void unlockAll();
    bool isIsolated() const { return isolated_; }
    void setIsolated(bool isolated);  // isole la sélection courante (sans effet si elle est vide)

    // --- Gizmo ----------------------------------------------------------------------------
    GizmoSpace gizmoSpace() const { return gizmoSpace_; }
    void setGizmoSpace(GizmoSpace space);
    void cycleGizmoSpace();  // Local -> Global -> Parent
    bool symmetryX() const { return symmetryX_; }
    void setSymmetryX(bool enabled);
    // Pivot et orientation du gizmo pour la sélection (os de plus haut niveau, non verrouillés) ; false si
    // rien à manipuler.
    bool gizmoFrame(glm::vec3& origin, glm::quat& orientation) const;

    // Manipulation interactive : begin capture l'état de départ, update applique un delta MONDE total
    // (absolu), end valide. La Phase 8 en fera une commande annulable.
    bool beginManipulation();
    void updateManipulation(const WorldDelta& delta);
    void endManipulation();
    bool isManipulating() const { return manipulating_; }

    // --- Édition du rig (mode Rig) ---------------------------------------------------------
    // Ajoute un os enfant de chaque os sélectionné (à son extrémité), ou un os racine si rien n'est
    // sélectionné ; le nouvel os devient la sélection. Avec la symétrie X, le côté opposé est créé aussi.
    Result<void> addBone();
    Result<void> subdivideSelected();
    Result<void> deleteSelected(bool withDescendants);

    // --- Pose ------------------------------------------------------------------------------
    void resetPose(bool selectedOnly);  // retour à la bind pose (modes Skin / Animate)
    void copyPose(bool selectedOnly);
    bool hasPoseClipboard() const { return !poseClipboard_.empty(); }
    void pastePose(bool mirrored);

signals:
    void characterChanged();
    void hierarchyChanged();  // parentés modifiées (les vues d'arbre se reconstruisent)
    void boneChanged(
        rigger::BoneId id);  // propriété d'un os (nom, transformation, longueur, couleur, drapeaux)
    void poseChanged();
    void selectionChanged();
    void modeChanged(rigger::editor::Mode mode);
    void toolChanged(rigger::editor::Tool tool);
    void clipChanged();
    void timeChanged();
    void playingChanged(bool playing);
    void skinMethodChanged();
    void visibilityChanged();  // masquage, verrouillage, isolation
    void meshDataChanged();    // poids de skinning modifiés (suppression d'os)
    void gizmoSpaceChanged();
    void symmetryChanged();
    void manipulationChanged(bool active);
    void animationChanged();  // clés, durée, clips, bibliothèque de poses
    void playbackSettingsChanged();
    void keySelectionChanged();
    void autoKeyChanged();
    void onionSkinChanged();
    void paintSettingsChanged();  // pinceau, normalisation, mode de peinture / de sélection
    void vertexSelectionChanged();
    void weightLocksChanged();
    void deformChanged();   // twist, contraintes, springs, drivers, auto-rig
    void mixChanged();      // couches, bandes, blend tree, mode de mélange
    void ikChanged();       // contrôles IK créés / modifiés / supprimés
    void historyChanged();  // pile d'annulation modifiée (entrée ajoutée, undo, redo, état propre)
    void warning(const QString& message);  // avertissement non bloquant à afficher dans la barre de statut

private:
    struct KeyDragState {
        bool active = false;
        KeyDragMode mode = KeyDragMode::Move;
        double pivot = 0.0;
        std::size_t clipIndex = 0;
        std::vector<KeyRef> refs;
        std::vector<KeyRef> current;
        std::unordered_map<BoneId, BoneTracks> before;
        double durationBefore = 0.0;
    };
    struct KeyItem {
        BoneId id;
        Transform value;
        unsigned mask;
    };
    AnimationClip* mutableClip();
    void syncPlayer();
    // Applique `mutate` à une copie des pistes des os donnés, puis enregistre une EditTracksCommand (avant /
    // après).
    bool editClipTracks(const char* name, const std::string& mergeKey, const std::vector<BoneId>& bones,
                        const std::function<void(AnimationClip&)>& mutate, bool recompute);
    bool keyTransforms(const std::vector<KeyItem>& items, const char* name, const std::string& mergeKey);
    double snappedTime() const;
    std::string uniqueClipName(const std::string& base) const;

    bool editWeights(const char* name, const std::function<void(Character&)>& edit,
                     const std::string& detail = {});
    Brush brush_;
    bool autoNormalize_ = true;
    bool paintVisibleOnly_ = true;
    bool paintActive_ = false;
    bool vertexSelectMode_ = false;
    VertexSelectShape vertexSelectShape_ = VertexSelectShape::Rectangle;
    WeightLocks weightLocks_;
    std::vector<VertexRef> vertexSelection_;
    bool strokeActive_ = false;
    std::vector<std::vector<SkinWeight>> strokeBefore_;
    std::vector<MeshTopology> strokeTopology_;
    std::vector<std::vector<std::uint32_t>> strokeMirror_;
    std::vector<std::vector<Vertex>> strokeSkinned_;
    struct IKBlend {
        bool active = false;
        int index = -1;
        float from = 0.0f;
        float to = 1.0f;
        double elapsed = 0.0;
        std::vector<IKControl> before;
    };
    bool commitIK(std::vector<IKControl> after, const char* name, const std::string& detail = {},
                  const std::string& mergeKey = {});
    void tickIKBlend();
    IKBlend ikBlend_;
    QTimer ikTimer_;
    mutable Pose evaluated_;
    bool forcedDirty_ = false;
    SpringState springState_;
    std::map<BoneId, glm::quat>
        springRotations_;      // rotations simulées des os des chaînes (appliquées par pose())
    bool simulating_ = false;  // vrai pendant l'avance de la lecture (pas de réinitialisation)
    void stepSimulation(double dt);
    void resetSprings();
    bool skipPoseRecompute_ = false;
    double keyTimeOverride_ = -1.0;
    AnimationPlayer player_;
    bool autoKey_ = false;
    std::vector<KeyRef> keySelection_;
    KeyDragState keyDrag_;
    std::vector<ClipKey> keyClipboard_;
    OnionSkin onion_;
    struct PoseEntry {
        std::string name;
        Transform local;
        glm::mat4 world;
    };
    // Exécute une commande via l'historique puis rafraîchit ce qu'elle a modifié.
    void runCommand(std::unique_ptr<Command> command);
    void refreshAfter(Effect effects, const std::vector<BoneId>& bones);
    void recordTransformDiff(const Pose& before, SetBoneTransformsCommand::Target target, const char* name);
    Result<void> editStructure(const char* name, const std::function<Result<void>()>& edit);
    Result<void> doAddBone();
    Result<void> doSubdivideSelected();
    Result<void> doDeleteSelected(bool withDescendants);
    Result<void> doReparent(BoneId id, BoneId newParent, bool keepWorld);
    EditContext editContext() { return {character_.get(), &pose_}; }

    History history_;
    UndoAdapter* undoAdapter_ = nullptr;
    Pose editPose() const;  // pose éditée : bind en mode Rig, pose courante sinon
    void commitPose(const Pose& pose, const std::vector<BoneId>& changed);
    void warnIfRigEditAffectsSkin();

    GizmoSpace gizmoSpace_ = GizmoSpace::Local;
    bool symmetryX_ = false;
    bool isolated_ = false;
    std::vector<BoneId> isolatedSet_;
    BoneId selectionAnchor_ = kNoBone;
    bool manipulating_ = false;
    std::vector<BoneId> manipulated_;
    Pose manipulationStart_;
    std::vector<glm::mat4> manipulationStartGlobals_;
    std::vector<PoseEntry> poseClipboard_;
    bool rigWarned_ = false;
    void recomputePose();
    void advance();

    std::shared_ptr<Character> character_;
    QString sourcePath_;
    Pose pose_;
    std::vector<BoneId> selection_;
    Mode mode_ = Mode::Animate;
    Tool tool_ = Tool::Select;
    SkinMethod skinMethod_ = SkinMethod::Lbs;
    int clipIndex_ = -1;
    double defaultFps_ = 30.0;
    double time_ = 0.0;
    bool playing_ = false;
    QTimer timer_;
    QElapsedTimer clock_;
};

}  // namespace rigger::editor

Q_DECLARE_METATYPE(rigger::editor::Mode)
Q_DECLARE_METATYPE(rigger::editor::Tool)
