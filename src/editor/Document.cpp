// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/Document.hpp"

#include <algorithm>
#include <cmath>

#include "core/animation/clip.hpp"
#include "core/character/edit.hpp"
#include "core/deform/morph.hpp"
#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"
#include "core/skeleton/symmetry.hpp"
#include "editor/CommandNames.hpp"
#include "editor/UndoAdapter.hpp"

namespace rigger::editor {

Document::Document(QObject* parent) : QObject(parent) {
    undoAdapter_ = new UndoAdapter(history_, this);
    undoAdapter_->setHandlers([this] { undo(); }, [this] { redo(); });
    connect(this, &Document::historyChanged, undoAdapter_, &UndoAdapter::sync);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &Document::advance);
}

std::size_t Document::vertexCount() const {
    std::size_t total = 0;
    if (character_) {
        for (const Mesh& mesh : character_->meshes) {
            total += mesh.vertices.size();
        }
    }
    return total;
}

void Document::setCharacter(std::shared_ptr<Character> character, const QString& sourcePath) {
    timer_.stop();
    playing_ = false;
    character_ = std::move(character);
    sourcePath_ = sourcePath;
    selection_.clear();
    selectionAnchor_ = kNoBone;
    isolated_ = false;
    isolatedSet_.clear();
    manipulating_ = false;
    history_.clear();
    forcedDirty_ = false;
    poseClipboard_.clear();
    rigWarned_ = false;
    time_ = 0.0;
    clipIndex_ = character_ && !character_->clips.empty() ? 0 : -1;
    keySelection_.clear();
    keyDrag_ = KeyDragState{};
    syncPlayer();
    mode_ = Mode::Animate;
    recomputePose();
    emit characterChanged();
    emit historyChanged();
    emit selectionChanged();
    emit modeChanged(mode_);
    emit clipChanged();
    emit timeChanged();
    emit playingChanged(false);
    if (clipIndex_ >= 0) {
        setPlaying(true);
    }
}

// --- Sélection ------------------------------------------------------------------------------

bool Document::isSelected(BoneId id) const {
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void Document::setSelection(std::vector<BoneId> ids) {
    std::vector<BoneId> clean;
    for (const BoneId id : ids) {
        if (character_ && character_->skeleton.contains(id) &&
            std::find(clean.begin(), clean.end(), id) == clean.end()) {
            clean.push_back(id);
        }
    }
    if (clean == selection_) {
        return;
    }
    selection_ = std::move(clean);
    history_.breakMerge();
    if (selection_.empty()) {
        selectionAnchor_ = kNoBone;
    } else if (std::find(selection_.begin(), selection_.end(), selectionAnchor_) == selection_.end()) {
        selectionAnchor_ = selection_.back();
    }
    emit selectionChanged();
}

// --- Mode / outil ---------------------------------------------------------------------------

void Document::setMode(Mode mode) {
    if (mode == mode_) {
        return;
    }
    mode_ = mode;
    if (mode_ != Mode::Animate && playing_) {
        setPlaying(false);
    }
    recomputePose();
    emit modeChanged(mode_);
    emit poseChanged();
}

void Document::setTool(Tool tool) {
    if (tool == tool_) {
        return;
    }
    tool_ = tool;
    emit toolChanged(tool_);
}

// --- Temps ----------------------------------------------------------------------------------

const AnimationClip* Document::clip() const {
    if (!character_ || clipIndex_ < 0 || static_cast<std::size_t>(clipIndex_) >= character_->clips.size()) {
        return nullptr;
    }
    return &character_->clips[static_cast<std::size_t>(clipIndex_)];
}

void Document::setClipIndex(int index) {
    const int count = character_ ? static_cast<int>(character_->clips.size()) : 0;
    index = std::clamp(index, -1, count - 1);
    if (index == clipIndex_) {
        return;
    }
    clipIndex_ = index;
    time_ = 0.0;
    keySelection_.clear();
    syncPlayer();
    if (clipIndex_ < 0) {
        setPlaying(false);
    }
    recomputePose();
    emit clipChanged();
    emit timeChanged();
    emit poseChanged();
}

double Document::fps() const {
    const AnimationClip* c = clip();
    return c != nullptr && c->fps > 0.0f ? static_cast<double>(c->fps) : defaultFps_;
}

void Document::setDefaultFps(double fps) {
    defaultFps_ = std::max(1.0, fps);
    emit timeChanged();
}

int Document::frame() const { return static_cast<int>(std::lround(time_ * fps())); }

int Document::frameCount() const {
    const AnimationClip* c = clip();
    return c != nullptr ? static_cast<int>(std::ceil(timelineDuration() * fps())) + 1 : 0;
}

void Document::setTime(double seconds) {
    const AnimationClip* c = clip();
    seconds = std::max(0.0, seconds);
    const double length = timelineDuration();
    if (c != nullptr && length > 0.0) {
        // Boucle : au-delà de la durée on revient au début ; l'image finale elle-même (t = durée) reste
        // atteignable.
        seconds =
            c->loop ? (seconds > length ? std::fmod(seconds, length) : seconds) : std::min(seconds, length);
    }
    time_ = seconds;
    player_.setTime(time_);
    recomputePose();
    emit timeChanged();
    emit poseChanged();
}

void Document::setFrame(int frame) {
    const int count = frameCount();
    if (count > 0) {
        frame = std::clamp(frame, 0, count - 1);
    } else {
        frame = std::max(frame, 0);
    }
    setTime(static_cast<double>(frame) / fps());
}

void Document::setPlaying(bool playing) {
    if (playing && clip() == nullptr) {
        playing = false;  // rien à lire
    }
    if (playing && mode_ != Mode::Animate) {
        setMode(Mode::Animate);
    }
    if (playing == playing_) {
        return;
    }
    playing_ = playing;
    if (playing_) {
        syncPlayer();
        player_.play();
        time_ = player_.time();
        clock_.restart();
        timer_.start();
    } else {
        player_.pause();
        timer_.stop();
    }
    emit playingChanged(playing_);
}

void Document::advance() {
    const double dt = static_cast<double>(clock_.restart()) / 1000.0;
    if (clip() == nullptr) {
        return;
    }
    player_.advance(dt);
    time_ = player_.time();
    simulating_ = true;
    recomputePose();
    stepSimulation(dt);
    simulating_ = false;
    emit timeChanged();
    emit poseChanged();
    if (!player_.isPlaying()) {
        setPlaying(false);  // fin de clip hors boucle
    }
}

// --- Skinning -------------------------------------------------------------------------------

void Document::setSkinMethod(SkinMethod method) {
    if (method == skinMethod_) {
        return;
    }
    skinMethod_ = method;
    emit skinMethodChanged();
}

// --- Pose -----------------------------------------------------------------------------------

void Document::recomputePose() {
    if (!simulating_)
        resetSprings();  // changement de temps / d'édition : la simulation repart de la pose animée
    if (!character_) {
        pose_.clear();
        return;
    }
    const AnimationClip* c = clip();
    if (mixActive()) {
        pose_ = evaluateMix(character_->skeleton, character_->clips, character_->mix, time_);
    } else if (mode_ == Mode::Animate && c != nullptr) {
        sampleClip(*c, character_->skeleton, time_, pose_);
    } else {
        pose_ = character_->skeleton.bindPose();
    }
}

// --- Éditions -------------------------------------------------------------------------------

// --- Annuler / rétablir ------------------------------------------------------------------------

namespace {
// Détail éventuel entre parenthèses (nom d'os) ajouté au nom traduit de la commande.
QString describe(const Command& command) {
    QString text = commandDisplayName(command.name());
    const std::string detail = command.detail();
    if (!detail.empty()) {
        text += QStringLiteral(" (%1)").arg(QString::fromStdString(detail));
    }
    return text;
}
}  // namespace

QString Document::undoText() const {
    return history_.nextUndo() != nullptr ? describe(*history_.nextUndo()) : QString();
}
QString Document::redoText() const {
    return history_.nextRedo() != nullptr ? describe(*history_.nextRedo()) : QString();
}
QUndoStack* Document::undoStack() const { return undoAdapter_->stack(); }

void Document::refreshAfter(Effect effects, const std::vector<BoneId>& bones) {
    if (hasEffect(effects, Effect::Animation) && character_) {
        // Les clips ont pu changer (undo / redo d'une suppression de clip, de clés, de durée...).
        const int count = static_cast<int>(character_->clips.size());
        clipIndex_ = count == 0 ? -1 : std::clamp(clipIndex_, 0, count - 1);
        syncPlayer();
        if (!skipPoseRecompute_) {
            recomputePose();
        }
        emit animationChanged();
    } else if (mode_ == Mode::Rig) {
        recomputePose();
    }
    if (mode_ == Mode::Rig && hasEffect(effects, Effect::Animation)) {
        recomputePose();
    }
    if (hasEffect(effects, Effect::Structure)) {
        setSelection(selection_);  // retire les os qui n'existent plus
        emit hierarchyChanged();
        emit meshDataChanged();
        emit visibilityChanged();
    } else if (hasEffect(effects, Effect::Weights)) {
        emit meshDataChanged();
    }
    if (hasEffect(effects, Effect::Properties)) {
        emit visibilityChanged();
    }
    if (hasEffect(effects, Effect::IK)) {
        emit ikChanged();
    }
    if (hasEffect(effects, Effect::Deform)) {
        emit deformChanged();
    }
    for (const BoneId id : bones) {
        if (character_ && character_->skeleton.contains(id)) {
            emit boneChanged(id);
        }
    }
    emit poseChanged();
    emit historyChanged();
}

void Document::runCommand(std::unique_ptr<Command> command) {
    const Effect effects = command->effects();
    const std::vector<BoneId> bones = command->affectedBones();
    EditContext context = editContext();
    history_.execute(std::move(command), context);
    refreshAfter(effects, bones);
}

void Document::undo() {
    if (!character_ || !canUndo()) {
        return;
    }
    EditContext context = editContext();
    if (const Command* done = history_.undo(context)) {
        refreshAfter(done->effects(), done->affectedBones());
    }
}

void Document::redo() {
    if (!character_ || !canRedo()) {
        return;
    }
    EditContext context = editContext();
    if (const Command* done = history_.redo(context)) {
        refreshAfter(done->effects(), done->affectedBones());
    }
}

void Document::recordTransformDiff(const Pose& before, SetBoneTransformsCommand::Target target,
                                   const char* name) {
    const Pose after = editPose();
    std::vector<SetBoneTransformsCommand::Entry> entries;
    for (const BoneId id : character_->skeleton.order()) {
        if (id < before.size() && id < after.size() &&
            (before[id].t != after[id].t || before[id].r != after[id].r || before[id].s != after[id].s)) {
            entries.push_back({id, before[id], after[id]});
        }
    }
    if (!entries.empty()) {
        history_.record(std::make_unique<SetBoneTransformsCommand>(target, std::move(entries), name));
        history_.breakMerge();
        emit historyChanged();
    }
}

Result<void> Document::editStructure(const char* name, const std::function<Result<void>()>& edit) {
    if (!character_) {
        return Error{"no character"};
    }
    const StructureState before = StructureState::capture(*character_);
    const std::uint64_t revision = character_->skeleton.revision();
    auto result = edit();
    if (!result) {
        if (character_->skeleton.revision() != revision) {
            // Échec en cours de route après une modification partielle : on remet exactement l'état de
            // départ.
            before.applyTo(*character_);
            recomputePose();
            emit hierarchyChanged();
            emit meshDataChanged();
            emit poseChanged();
        }
        return result;
    }
    if (character_->skeleton.revision() != revision) {
        history_.record(
            std::make_unique<StructureCommand>(name, "", before, StructureState::capture(*character_)));
        history_.breakMerge();
        emit historyChanged();
    }
    return result;
}

bool Document::setBonesLocal(const std::vector<std::pair<BoneId, Transform>>& edits,
                             const std::string& mergeKey) {
    if (!character_) {
        return false;
    }
    const auto target =
        mode_ == Mode::Rig ? SetBoneTransformsCommand::Target::Bind : SetBoneTransformsCommand::Target::Pose;
    std::vector<SetBoneTransformsCommand::Entry> entries;
    for (const auto& [id, local] : edits) {
        if (!character_->skeleton.contains(id) ||
            hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Locked) ||
            (target == SetBoneTransformsCommand::Target::Pose && id >= pose_.size())) {
            continue;
        }
        entries.push_back({id, boneLocal(id), local});
    }
    if (entries.empty()) {
        return false;
    }
    warnIfRigEditAffectsSkin();
    if (autoKey_ && mode_ == Mode::Animate) {
        // Auto-key : seules les composantes modifiées reçoivent une clé.
        std::vector<KeyItem> items;
        for (const auto& e : entries) {
            unsigned mask = 0;
            if (e.before.t != e.after.t) mask |= 1u << ChannelTranslation;
            if (std::abs(glm::dot(e.before.r, e.after.r)) < 1.0f - 1e-7f) mask |= 1u << ChannelRotation;
            if (e.before.s != e.after.s) mask |= 1u << ChannelScale;
            if (mask != 0) items.push_back({e.id, e.after, mask});
        }
        return keyTransforms(items, "Insert Keys", mergeKey.empty() ? std::string() : "key:" + mergeKey);
    }
    runCommand(
        std::make_unique<SetBoneTransformsCommand>(target, std::move(entries), "Transform Bones", mergeKey));
    return true;
}

bool Document::setBonesProperties(const std::vector<BoneId>& ids,
                                  const std::function<void(BoneProps&)>& modify, const std::string& name,
                                  const std::string& mergeKey, bool ignoreLock) {
    if (!character_) {
        return false;
    }
    std::vector<SetBonePropertiesCommand::Entry> entries;
    for (const BoneId id : ids) {
        if (!character_->skeleton.contains(id) ||
            (!ignoreLock && hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Locked))) {
            continue;
        }
        const BoneProps before = BoneProps::of(character_->skeleton.bone(id));
        BoneProps after = before;
        modify(after);
        if (!(before == after)) {
            entries.push_back({id, before, after});
        }
    }
    if (entries.empty()) {
        return false;
    }
    runCommand(std::make_unique<SetBonePropertiesCommand>(std::move(entries), name, mergeKey));
    return true;
}

Result<void> Document::renameBone(BoneId id, const QString& name) {
    if (!character_ || !character_->skeleton.contains(id)) {
        return Error{"unknown bone"};
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return Error{"bone names cannot be empty"};
    }
    setBonesProperties({id}, [&](BoneProps& p) { p.name = trimmed.toStdString(); }, "Rename Bone", {}, true);
    return {};
}

Result<void> Document::reparentBone(BoneId id, BoneId newParent, bool keepWorld) {
    return editStructure("Reparent Bone", [&] { return doReparent(id, newParent, keepWorld); });
}

bool Document::setBoneLocal(BoneId id, const Transform& local) {
    return setBonesLocal({{id, local}}, "local:" + std::to_string(id) + (mode_ == Mode::Rig ? "b" : "p"));
}

bool Document::setBoneLength(BoneId id, float length) {
    return setBonesProperties(
        {id}, [&](BoneProps& p) { p.length = std::max(length, 1e-4f); }, "Edit Bone",
        "length:" + std::to_string(id));
}

bool Document::setBoneColor(BoneId id, const glm::vec3& color) {
    return setBonesProperties(
        {id}, [&](BoneProps& p) { p.color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)); },
        "Edit Bone", "color:" + std::to_string(id));
}

bool Document::setBoneFlags(BoneId id, BoneFlags flags) {
    return setBonesProperties({id}, [&](BoneProps& p) { p.flags = flags; }, "Change Bone Flags", {}, true);
}

Transform Document::boneLocal(BoneId id) const {
    if (!character_ || !character_->skeleton.contains(id)) {
        return {};
    }
    if (mode_ == Mode::Rig || id >= pose_.size()) {
        return character_->skeleton.bone(id).bindLocal;
    }
    return pose_[id];
}

// --- Visibilité, sélection avancée -------------------------------------------------------------

bool Document::isBoneVisible(BoneId id) const {
    if (!character_ || !character_->skeleton.contains(id)) {
        return false;
    }
    if (hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Hidden)) {
        return false;
    }
    return !isolated_ || std::find(isolatedSet_.begin(), isolatedSet_.end(), id) != isolatedSet_.end();
}

bool Document::isBonePickable(BoneId id) const {
    return isBoneVisible(id) && !hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Locked);
}

void Document::selectAll() {
    std::vector<BoneId> ids;
    if (character_) {
        for (const BoneId id : character_->skeleton.order()) {
            if (isBonePickable(id)) {
                ids.push_back(id);
            }
        }
    }
    setSelection(std::move(ids));
}

void Document::selectRangeTo(BoneId target) {
    if (!character_ || !character_->skeleton.contains(target)) {
        return;
    }
    const auto& order = character_->skeleton.order();
    const auto targetPos = std::find(order.begin(), order.end(), target);
    auto anchorPos = std::find(order.begin(), order.end(), selectionAnchor_);
    if (anchorPos == order.end()) {
        anchorPos = targetPos;
    }
    const auto [lo, hi] = std::minmax(anchorPos, targetPos);
    std::vector<BoneId> ids;
    for (auto it = lo; it <= hi; ++it) {
        if (isBonePickable(*it)) {
            ids.push_back(*it);
        }
    }
    const BoneId keepAnchor = selectionAnchor_;
    setSelection(std::move(ids));
    selectionAnchor_ = keepAnchor == kNoBone ? target : keepAnchor;
}

void Document::toggleInSelection(BoneId id) {
    std::vector<BoneId> ids = selection_;
    const auto it = std::find(ids.begin(), ids.end(), id);
    if (it != ids.end()) {
        ids.erase(it);
    } else {
        ids.push_back(id);
    }
    setSelection(std::move(ids));
}

void Document::hideSelected() {
    setBonesProperties(
        selection_, [](BoneProps& p) { p.flags = p.flags | BoneFlags::Hidden; }, "Hide Bones", {}, true);
}

void Document::showAll() {
    if (!character_) {
        return;
    }
    setIsolated(false);
    const std::vector<BoneId> all(character_->skeleton.order());
    setBonesProperties(
        all,
        [](BoneProps& p) {
            p.flags = static_cast<BoneFlags>(static_cast<std::uint32_t>(p.flags) &
                                             ~static_cast<std::uint32_t>(BoneFlags::Hidden));
        },
        "Show All Bones", {}, true);
}

void Document::lockSelected() {
    setBonesProperties(
        selection_, [](BoneProps& p) { p.flags = p.flags | BoneFlags::Locked; }, "Lock Bones", {}, true);
}

void Document::unlockAll() {
    if (!character_) {
        return;
    }
    const std::vector<BoneId> all(character_->skeleton.order());
    setBonesProperties(
        all,
        [](BoneProps& p) {
            p.flags = static_cast<BoneFlags>(static_cast<std::uint32_t>(p.flags) &
                                             ~static_cast<std::uint32_t>(BoneFlags::Locked));
        },
        "Unlock All Bones", {}, true);
}

void Document::setIsolated(bool isolated) {
    if (isolated && (selection_.empty() || !character_)) {
        isolated = false;
    }
    if (isolated == isolated_) {
        return;
    }
    isolated_ = isolated;
    isolatedSet_ = isolated ? selection_ : std::vector<BoneId>{};
    emit visibilityChanged();
    emit poseChanged();
}

// --- Gizmo -----------------------------------------------------------------------------------

void Document::setGizmoSpace(GizmoSpace space) {
    if (space == gizmoSpace_) {
        return;
    }
    gizmoSpace_ = space;
    emit gizmoSpaceChanged();
    emit poseChanged();
}

void Document::cycleGizmoSpace() {
    switch (gizmoSpace_) {
        case GizmoSpace::Local:
            setGizmoSpace(GizmoSpace::Global);
            break;
        case GizmoSpace::Global:
            setGizmoSpace(GizmoSpace::Parent);
            break;
        case GizmoSpace::Parent:
            setGizmoSpace(GizmoSpace::Local);
            break;
    }
}

void Document::setSymmetryX(bool enabled) {
    if (enabled == symmetryX_) {
        return;
    }
    symmetryX_ = enabled;
    emit symmetryChanged();
}

Pose Document::editPose() const {
    if (!character_) {
        return {};
    }
    return mode_ == Mode::Rig ? character_->skeleton.bindPose() : pose_;
}

bool Document::gizmoFrame(glm::vec3& origin, glm::quat& orientation) const {
    if (!character_) {
        return false;
    }
    std::vector<BoneId> ids;
    for (const BoneId id : selection_) {
        if (!hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Locked)) {
            ids.push_back(id);
        }
    }
    ids = topLevelBones(character_->skeleton, ids);
    if (ids.empty()) {
        return false;
    }
    const auto globals = computeGlobals(character_->skeleton, editPose());
    origin = selectionPivot(globals, ids);
    orientation = spaceOrientation(character_->skeleton, globals, ids.back(), gizmoSpace_);
    return true;
}

// --- Manipulation interactive -----------------------------------------------------------------

void Document::warnIfRigEditAffectsSkin() {
    if (rigWarned_ || mode_ != Mode::Rig || !character_) {
        return;
    }
    const bool skinned = std::any_of(character_->meshes.begin(), character_->meshes.end(),
                                     [](const Mesh& mesh) { return mesh.hasSkinning(); });
    if (skinned) {
        rigWarned_ = true;
        emit warning(
            tr("Editing the bind pose of a skinned character keeps its skinning weights: the mesh will "
               "deform in "
               "Animate mode."));
    }
}

void Document::commitPose(const Pose& pose, const std::vector<BoneId>& changed) {
    if (mode_ == Mode::Rig) {
        for (const BoneId id : changed) {
            character_->skeleton.setBindLocal(id, pose[id]);
        }
        recomputePose();
    } else {
        pose_ = pose;
    }
    for (const BoneId id : changed) {
        emit boneChanged(id);
    }
    emit poseChanged();
}

bool Document::beginManipulation() {
    if (!character_ || manipulating_) {
        return false;
    }
    std::vector<BoneId> ids;
    for (const BoneId id : selection_) {
        if (!hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Locked)) {
            ids.push_back(id);
        }
    }
    manipulated_ = topLevelBones(character_->skeleton, ids);
    if (manipulated_.empty()) {
        return false;
    }
    manipulationStart_ = editPose();
    manipulationStartGlobals_ = computeGlobals(character_->skeleton, manipulationStart_);
    manipulating_ = true;
    warnIfRigEditAffectsSkin();
    emit manipulationChanged(true);
    return true;
}

void Document::updateManipulation(const WorldDelta& delta) {
    if (!manipulating_) {
        return;
    }
    const Skeleton& skeleton = character_->skeleton;
    Pose work = manipulationStart_;
    std::vector<BoneId> changed;
    for (const BoneId id : manipulated_) {
        work[id] = applyWorldDelta(skeleton, manipulationStartGlobals_, manipulationStart_[id], id, delta);
        changed.push_back(id);
    }
    if (symmetryX_) {
        // Le côté opposé reçoit la transformation miroir du monde de l'os source (le parent d'abord).
        for (const BoneId id : skeleton.order()) {
            if (std::find(manipulated_.begin(), manipulated_.end(), id) == manipulated_.end()) {
                continue;
            }
            const BoneId mirror = findMirrorBone(skeleton, id);
            if (mirror == kNoBone ||
                std::find(manipulated_.begin(), manipulated_.end(), mirror) != manipulated_.end() ||
                hasFlag(skeleton.bone(mirror).flags, BoneFlags::Locked)) {
                continue;
            }
            const auto globals = computeGlobals(skeleton, work);
            work[mirror] = mirroredLocal(skeleton, globals, mirror, globals[id]);
            changed.push_back(mirror);
        }
    }
    commitPose(work, changed);
}

void Document::endManipulation() {
    if (!manipulating_) {
        return;
    }
    // Le glissé a modifié l'état en direct ; l'historique reçoit UNE commande (départ -> arrivée).
    const Pose start = manipulationStart_;
    manipulating_ = false;
    manipulated_.clear();
    if (autoKey_ && mode_ == Mode::Animate && character_) {
        // Auto-key : les os modifiés par le glissé reçoivent des clés (T / R / S selon ce qui a changé).
        std::vector<KeyItem> items;
        for (const BoneId id : character_->skeleton.order()) {
            if (id >= start.size() || id >= pose_.size()) continue;
            unsigned mask = 0;
            if (start[id].t != pose_[id].t) mask |= 1u << ChannelTranslation;
            if (std::abs(glm::dot(start[id].r, pose_[id].r)) < 1.0f - 1e-7f) mask |= 1u << ChannelRotation;
            if (start[id].s != pose_[id].s) mask |= 1u << ChannelScale;
            if (mask != 0) items.push_back({id, pose_[id], mask});
        }
        keyTransforms(items, "Insert Keys", {});
        emit manipulationChanged(false);
        return;
    }
    recordTransformDiff(
        start,
        mode_ == Mode::Rig ? SetBoneTransformsCommand::Target::Bind : SetBoneTransformsCommand::Target::Pose,
        "Transform Bones");
    emit manipulationChanged(false);
}

// --- Édition du rig ---------------------------------------------------------------------------

Result<void> Document::addBone() {
    return editStructure("Add Bone", [this] { return doAddBone(); });
}

Result<void> Document::doAddBone() {
    if (!character_) {
        return Error{"no character"};
    }
    if (mode_ != Mode::Rig) {
        return Error{"bones can only be added in Rig mode"};
    }
    Skeleton& skeleton = character_->skeleton;
    warnIfRigEditAffectsSkin();
    std::vector<BoneId> parents = topLevelBones(skeleton, selection_);
    std::vector<BoneId> created;
    const auto makeChild = [&](BoneId parent, const std::string& name) -> Result<BoneId> {
        Transform local;
        float length = 0.2f;
        if (parent != kNoBone) {
            local.t = glm::vec3(0.0f, skeleton.bone(parent).length, 0.0f);  // à l'extrémité du parent
            length = skeleton.bone(parent).length;
        }
        auto id = skeleton.addBone(name, parent, local);
        if (id) {
            skeleton.setLength(id.value(), length);
            if (parent != kNoBone) {
                skeleton.setColor(id.value(), skeleton.bone(parent).color);
            }
        }
        return id;
    };
    if (parents.empty()) {
        auto id = makeChild(kNoBone, "Bone");
        if (!id) {
            return id.error();
        }
        created.push_back(id.value());
    }
    for (const BoneId parent : parents) {
        const std::string parentName = skeleton.bone(parent).name;
        const auto mirrorParentName = mirrorName(parentName);
        auto id = makeChild(parent, mirrorParentName ? parentName + ".child" : "Bone");
        if (!id) {
            return id.error();
        }
        created.push_back(id.value());
        if (symmetryX_ && mirrorParentName) {
            const BoneId mirrorParent = skeleton.find(*mirrorParentName);
            if (mirrorParent != kNoBone && mirrorParent != parent) {
                auto twin = makeChild(mirrorParent, *mirrorParentName + ".child");
                if (!twin) {
                    return twin.error();
                }
                created.push_back(twin.value());
            }
        }
    }
    recomputePose();
    emit hierarchyChanged();
    emit poseChanged();
    setSelection({created.front()});
    return {};
}

Result<void> Document::subdivideSelected() {
    return editStructure("Subdivide Bone", [this] { return doSubdivideSelected(); });
}

Result<void> Document::doSubdivideSelected() {
    if (!character_ || mode_ != Mode::Rig) {
        return Error{"bones can only be subdivided in Rig mode"};
    }
    warnIfRigEditAffectsSkin();
    std::vector<BoneId> targets = selection_;
    if (symmetryX_) {
        for (const BoneId id : std::vector<BoneId>(selection_)) {
            const BoneId mirror = findMirrorBone(character_->skeleton, id);
            if (mirror != kNoBone && std::find(targets.begin(), targets.end(), mirror) == targets.end()) {
                targets.push_back(mirror);
            }
        }
    }
    std::vector<BoneId> created;
    for (const BoneId id : targets) {
        auto mid = subdivideBone(*character_, id);
        if (!mid) {
            return mid.error();
        }
        created.push_back(mid.value());
    }
    recomputePose();
    emit hierarchyChanged();
    emit poseChanged();
    setSelection(created);
    return {};
}

Result<void> Document::deleteSelected(bool withDescendants) {
    return editStructure("Delete Bone",
                         [this, withDescendants] { return doDeleteSelected(withDescendants); });
}

Result<void> Document::doDeleteSelected(bool withDescendants) {
    if (!character_ || mode_ != Mode::Rig) {
        return Error{"bones can only be deleted in Rig mode"};
    }
    std::vector<BoneId> targets = selection_;
    if (targets.empty()) {
        return {};
    }
    warnIfRigEditAffectsSkin();
    for (const BoneId id : targets) {
        if (!character_->skeleton.contains(id)) {
            continue;  // déjà supprimé avec un ancêtre
        }
        auto result = removeBoneClean(
            *character_, id, withDescendants ? RemoveMode::WithDescendants : RemoveMode::ReparentChildren);
        if (!result) {
            return result;
        }
    }
    isolated_ = false;
    isolatedSet_.clear();
    selection_.clear();
    recomputePose();
    emit hierarchyChanged();
    emit meshDataChanged();
    emit selectionChanged();
    emit visibilityChanged();
    emit poseChanged();
    return {};
}

// --- Pose -------------------------------------------------------------------------------------

void Document::resetPose(bool selectedOnly) {
    if (!character_ || mode_ == Mode::Rig) {
        return;
    }
    const Skeleton& skeleton = character_->skeleton;
    const Pose before = editPose();
    std::vector<BoneId> ids = selectedOnly ? selection_ : std::vector<BoneId>(skeleton.order());
    std::vector<BoneId> changed;
    for (const BoneId id : ids) {
        if (!skeleton.contains(id) || hasFlag(skeleton.bone(id).flags, BoneFlags::Locked) ||
            id >= pose_.size()) {
            continue;
        }
        pose_[id] = skeleton.bone(id).bindLocal;
        changed.push_back(id);
    }
    for (const BoneId id : changed) {
        emit boneChanged(id);
    }
    emit poseChanged();
    recordTransformDiff(before, SetBoneTransformsCommand::Target::Pose, "Reset Pose");
}

void Document::copyPose(bool selectedOnly) {
    if (!character_) {
        return;
    }
    const Skeleton& skeleton = character_->skeleton;
    const Pose pose = editPose();
    const auto globals = computeGlobals(skeleton, pose);
    poseClipboard_.clear();
    const std::vector<BoneId> ids =
        selectedOnly && !selection_.empty() ? selection_ : std::vector<BoneId>(skeleton.order());
    for (const BoneId id : ids) {
        if (skeleton.contains(id) && id < pose.size()) {
            poseClipboard_.push_back({skeleton.bone(id).name, pose[id], globals[id]});
        }
    }
}

void Document::pastePose(bool mirrored) {
    if (!character_ || poseClipboard_.empty()) {
        return;
    }
    const Skeleton& skeleton = character_->skeleton;
    const Pose before = editPose();
    Pose work = before;
    std::vector<BoneId> changed;
    // Dans l'ordre hiérarchique de la cible : un parent miroir est posé avant ses enfants.
    for (const BoneId target : skeleton.order()) {
        if (hasFlag(skeleton.bone(target).flags, BoneFlags::Locked)) {
            continue;
        }
        const std::string& targetName = skeleton.bone(target).name;
        // Pour un collage miroir, la source du os « arm.L » est l'entrée « arm.R » ; un os central se reflète
        // lui-même.
        std::string sourceName = targetName;
        if (mirrored) {
            if (const auto m = mirrorName(targetName)) {
                sourceName = *m;
            }
        }
        const auto entry = std::find_if(poseClipboard_.begin(), poseClipboard_.end(),
                                        [&](const PoseEntry& e) { return e.name == sourceName; });
        if (entry == poseClipboard_.end()) {
            continue;
        }
        if (mirrored) {
            const auto globals = computeGlobals(skeleton, work);
            work[target] = mirroredLocal(skeleton, globals, target, entry->world);
        } else {
            work[target] = entry->local;
        }
        changed.push_back(target);
    }
    if (!changed.empty()) {
        warnIfRigEditAffectsSkin();
        commitPose(work, changed);
        recordTransformDiff(before,
                            mode_ == Mode::Rig ? SetBoneTransformsCommand::Target::Bind
                                               : SetBoneTransformsCommand::Target::Pose,
                            "Paste Pose");
    }
}

Result<void> Document::doReparent(BoneId id, BoneId newParent, bool keepWorld) {
    if (!character_ || !character_->skeleton.contains(id)) {
        return Error{"unknown bone"};
    }
    if (character_->skeleton.bone(id).parent == newParent) {
        return {};
    }
    auto result = character_->skeleton.reparent(id, newParent, keepWorld);
    if (result) {
        recomputePose();
        emit hierarchyChanged();
        emit poseChanged();
    }
    return result;
}

// --- Animation : lecture --------------------------------------------------------------------------

void Document::syncPlayer() {
    const AnimationClip* c = clip();
    player_.setDuration(c != nullptr ? timelineDuration() : 0.0);
    player_.setTime(time_);
}

void Document::setPlaybackSpeed(double speed) {
    player_.setSpeed(speed);
    emit playbackSettingsChanged();
}

void Document::setLoopPlayback(bool loop) {
    player_.setLoop(loop);
    emit playbackSettingsChanged();
}

void Document::setPingPong(bool pingPong) {
    player_.setPingPong(pingPong);
    emit playbackSettingsChanged();
}

void Document::setPlaybackRange(double start, double end) {
    syncPlayer();
    player_.setRange(start, end);
    emit playbackSettingsChanged();
}

void Document::setAutoKey(bool enabled) {
    if (enabled == autoKey_) {
        return;
    }
    autoKey_ = enabled;
    emit autoKeyChanged();
}

void Document::setOnionSkin(const OnionSkin& onion) {
    onion_ = onion;
    onion_.before = std::clamp(onion_.before, 0, 10);
    onion_.after = std::clamp(onion_.after, 0, 10);
    onion_.step = std::clamp(onion_.step, 1, 30);
    emit onionSkinChanged();
    emit poseChanged();
}

void Document::setKeySelection(std::vector<KeyRef> keys) {
    if (keys == keySelection_) {
        return;
    }
    keySelection_ = std::move(keys);
    emit keySelectionChanged();
}

double Document::snappedTime() const { return std::round(time_ * fps()) / fps(); }

AnimationClip* Document::mutableClip() {
    return const_cast<AnimationClip*>(static_cast<const Document*>(this)->clip());
}

std::vector<BoneKeyState> Document::boneKeyStates() const {
    std::vector<BoneKeyState> states;
    const AnimationClip* c = clip();
    if (!character_ || c == nullptr || mode_ != Mode::Animate) {
        return states;
    }
    const Skeleton& skeleton = character_->skeleton;
    states.assign(skeleton.slotCount(), BoneKeyState::None);
    const Pose sampled = samplePose(*c, skeleton, time_);
    const double t = snappedTime();
    for (const BoneId id : skeleton.order()) {
        const auto it = c->tracks.find(id);
        if (it != c->tracks.end() && keyedChannels(it->second, t) != 0) {
            states[id] = BoneKeyState::Keyed;
        } else if (id < pose_.size() && id < sampled.size()) {
            const Transform& a = pose_[id];
            const Transform& b = sampled[id];
            if (glm::length(a.t - b.t) > 1e-5f || std::abs(glm::dot(a.r, b.r)) < 1.0f - 1e-6f ||
                glm::length(a.s - b.s) > 1e-5f) {
                states[id] = BoneKeyState::Modified;
            }
        }
    }
    return states;
}

// --- Animation : édition des pistes ---------------------------------------------------------------

bool Document::editClipTracks(const char* name, const std::string& mergeKey, const std::vector<BoneId>& bones,
                              const std::function<void(AnimationClip&)>& mutate, bool recompute) {
    AnimationClip* live = mutableClip();
    if (live == nullptr) {
        return false;
    }
    AnimationClip work;
    work.duration = live->duration;
    work.fps = live->fps;
    for (const BoneId id : bones) {
        if (const auto it = live->tracks.find(id); it != live->tracks.end()) {
            work.tracks[id] = it->second;
        }
    }
    mutate(work);
    std::vector<EditTracksCommand::Entry> entries;
    bool changed = work.duration != live->duration;
    std::vector<BoneId> touched = bones;
    for (const auto& [id, tracks] : work.tracks) {
        if (std::find(touched.begin(), touched.end(), id) == touched.end()) touched.push_back(id);
    }
    for (const BoneId id : touched) {
        const auto before = live->tracks.find(id);
        const auto after = work.tracks.find(id);
        BoneTracks b = before != live->tracks.end() ? before->second : BoneTracks{};
        BoneTracks a = after != work.tracks.end() ? after->second : BoneTracks{};
        entries.push_back({id, std::move(b), std::move(a)});
    }
    // Rien n'a changé ? (comparaison grossière : nombre de clés et temps / valeurs de translation)
    if (!changed) {
        for (const auto& e : entries) {
            const auto count = [](const BoneTracks& t) {
                return t.translation.keys.size() + t.rotation.keys.size() + t.scale.keys.size();
            };
            if (count(e.before) != count(e.after)) {
                changed = true;
                break;
            }
            changed = changed ||
                      !std::equal(e.before.translation.keys.begin(), e.before.translation.keys.end(),
                                  e.after.translation.keys.begin(), [](const auto& x, const auto& y) {
                                      return x.time == y.time && x.value == y.value && x.mode == y.mode &&
                                             x.inTangent == y.inTangent && x.outTangent == y.outTangent;
                                  });
            changed =
                changed || !std::equal(e.before.rotation.keys.begin(), e.before.rotation.keys.end(),
                                       e.after.rotation.keys.begin(), [](const auto& x, const auto& y) {
                                           return x.time == y.time && x.value == y.value && x.mode == y.mode;
                                       });
            changed =
                changed || !std::equal(e.before.scale.keys.begin(), e.before.scale.keys.end(),
                                       e.after.scale.keys.begin(), [](const auto& x, const auto& y) {
                                           return x.time == y.time && x.value == y.value && x.mode == y.mode;
                                       });
            if (changed) break;
        }
    }
    if (!changed) {
        return false;
    }
    const double durationBefore = live->duration;
    const double durationAfter = work.duration;
    const Effect effects = Effect::Animation;
    const std::vector<BoneId> ids = touched;
    EditContext context = editContext();
    history_.execute(
        std::make_unique<EditTracksCommand>(static_cast<std::size_t>(clipIndex_), std::move(entries),
                                            durationBefore, durationAfter, name, mergeKey),
        context);
    skipPoseRecompute_ = !recompute;
    refreshAfter(effects, ids);
    skipPoseRecompute_ = false;
    return true;
}

bool Document::keyTransforms(const std::vector<KeyItem>& items, const char* name,
                             const std::string& mergeKey) {
    if (!character_ || items.empty()) {
        return false;
    }
    const bool created = clip() == nullptr;
    const double t = keyTimeOverride_ >= 0.0 ? keyTimeOverride_ : snappedTime();
    if (created) {
        const double savedTime = time_;
        history_.beginGroup(name);
        if (!newClip()) {
            history_.endGroup();
            return false;
        }
        time_ = savedTime;  // le clip créé automatiquement ne ramène pas la tête de lecture à 0
        syncPlayer();
    }
    std::vector<BoneId> bones;
    for (const KeyItem& item : items) bones.push_back(item.id);
    const bool ok = editClipTracks(
        name, mergeKey, bones,
        [&](AnimationClip& work) {
            for (const KeyItem& item : items) {
                keyTransform(work.tracks[item.id], t, item.value, item.mask);
            }
            extendDuration(work);
        },
        false);
    for (const KeyItem& item : items) {
        if (item.id < pose_.size())
            pose_[item.id] = item.value;  // la pose affichée reste celle qu'on vient de keyer
    }
    if (created) {
        history_.endGroup();
    }
    emit poseChanged();
    return ok;
}

bool Document::insertKeys(const std::vector<BoneId>& bones, unsigned channelMask) {
    if (!character_ || mode_ != Mode::Animate) {
        return false;
    }
    std::vector<KeyItem> items;
    for (const BoneId id : bones) {
        if (character_->skeleton.contains(id) && id < pose_.size() &&
            !hasFlag(character_->skeleton.bone(id).flags, BoneFlags::Locked)) {
            items.push_back({id, pose_[id], channelMask});
        }
    }
    return keyTransforms(items, "Insert Keys", {});
}

bool Document::insertKeysAt(const std::vector<BoneId>& bones, unsigned channelMask, double seconds) {
    keyTimeOverride_ = std::max(seconds, 0.0);
    const bool ok = insertKeys(bones, channelMask);
    keyTimeOverride_ = -1.0;
    return ok;
}

std::size_t Document::deleteKeys(const std::vector<KeyRef>& keys) {
    if (clip() == nullptr || keys.empty()) {
        return 0;
    }
    std::vector<BoneId> bones;
    for (const KeyRef& k : keys) {
        if (std::find(bones.begin(), bones.end(), k.bone) == bones.end()) bones.push_back(k.bone);
    }
    std::size_t erased = 0;
    editClipTracks(
        "Delete Keys", {}, bones, [&](AnimationClip& work) { erased = eraseKeys(work, keys); }, true);
    setKeySelection({});
    return erased;
}

bool Document::setKeysInterpolation(const std::vector<KeyRef>& keys, Interpolation mode) {
    if (clip() == nullptr || keys.empty()) {
        return false;
    }
    std::vector<BoneId> bones;
    for (const KeyRef& k : keys) {
        if (std::find(bones.begin(), bones.end(), k.bone) == bones.end()) bones.push_back(k.bone);
    }
    return editClipTracks(
        "Set Interpolation", {}, bones, [&](AnimationClip& work) { setKeyInterpolation(work, keys, mode); },
        true);
}

bool Document::editKey(const KeyRef& key, const KeyEdit& edit, bool merge) {
    if (clip() == nullptr) {
        return false;
    }
    KeyRef newRef = key;
    const bool ok = editClipTracks(
        "Edit Key",
        merge ? "key:" + std::to_string(key.bone) + ":" + std::to_string(key.channel) : std::string(),
        {key.bone},
        [&](AnimationClip& work) {
            BoneTracks& tracks = work.tracks[key.bone];
            const auto apply = [&](auto& track, auto setValue) {
                const auto index = findKey(track, key.time);
                if (!index) {
                    return;
                }
                auto k = track.keys[*index];
                if (edit.value) setValue(k, *edit.value);
                if constexpr (std::is_same_v<std::decay_t<decltype(k.value)>, glm::vec3>) {
                    if (edit.inTangent) k.inTangent = *edit.inTangent;
                    if (edit.outTangent) k.outTangent = *edit.outTangent;
                }
                if (edit.time) {
                    track.keys.erase(track.keys.begin() + static_cast<std::ptrdiff_t>(*index));
                    k.time = std::max(*edit.time, 0.0);
                    newRef.time = k.time;
                    insertKey(track, k);
                } else {
                    track.keys[*index] = k;
                }
            };
            if (key.channel == ChannelRotation) {
                apply(tracks.rotation,
                      [](Keyframe<glm::quat>& k, const glm::vec3& euler) { k.value = fromEulerXYZ(euler); });
            } else {
                apply(key.channel == ChannelScale ? tracks.scale : tracks.translation,
                      [](Keyframe<glm::vec3>& k, const glm::vec3& v) { k.value = v; });
            }
            extendDuration(work);
        },
        true);
    if (ok && edit.time) {
        std::vector<KeyRef> selection = keySelection_;
        for (KeyRef& k : selection) {
            if (k == key) k = newRef;
        }
        setKeySelection(selection);
    }
    return ok;
}

// --- Glissé de clés ---------------------------------------------------------------------------

bool Document::beginKeyDrag(const std::vector<KeyRef>& keys, KeyDragMode mode, double pivot) {
    AnimationClip* live = mutableClip();
    if (live == nullptr || keys.empty() || keyDrag_.active || manipulating_) {
        return false;
    }
    keyDrag_ = KeyDragState{};
    keyDrag_.active = true;
    keyDrag_.mode = mode;
    keyDrag_.pivot = pivot;
    keyDrag_.clipIndex = static_cast<std::size_t>(clipIndex_);
    keyDrag_.refs = keys;
    keyDrag_.current = keys;
    keyDrag_.durationBefore = live->duration;
    for (const KeyRef& k : keys) {
        if (const auto it = live->tracks.find(k.bone); it != live->tracks.end()) {
            keyDrag_.before[k.bone] = it->second;
        }
    }
    return true;
}

void Document::updateKeyDrag(double value) {
    AnimationClip* live = mutableClip();
    if (!keyDrag_.active || live == nullptr) {
        return;
    }
    AnimationClip work;
    work.duration = keyDrag_.durationBefore;
    work.tracks = keyDrag_.before;
    const KeyDragMode mode = keyDrag_.mode;
    const double pivot = keyDrag_.pivot;
    keyDrag_.current = remapKeys(
        work, keyDrag_.refs,
        [&](double t) { return mode == KeyDragMode::Scale ? pivot + (t - pivot) * value : t + value; },
        mode == KeyDragMode::Duplicate);
    for (const auto& [bone, tracks] : keyDrag_.before) {
        const auto it = work.tracks.find(bone);
        if (it == work.tracks.end() || it->second.empty()) {
            live->tracks.erase(bone);
        } else {
            live->tracks[bone] = it->second;
        }
    }
    for (const auto& [bone, tracks] : work.tracks) {  // pistes créées par le glissé
        if (!keyDrag_.before.count(bone)) live->tracks[bone] = tracks;
    }
    live->duration = work.duration;
    setKeySelection(keyDrag_.current);
    recomputePose();
    syncPlayer();
    emit animationChanged();
    emit poseChanged();
}

void Document::endKeyDrag() {
    if (!keyDrag_.active) {
        return;
    }
    AnimationClip* live = mutableClip();
    KeyDragState state = std::move(keyDrag_);
    keyDrag_ = KeyDragState{};
    if (live == nullptr) {
        return;
    }
    std::vector<EditTracksCommand::Entry> entries;
    bool changed = live->duration != state.durationBefore;
    for (const auto& [bone, before] : state.before) {
        const auto it = live->tracks.find(bone);
        BoneTracks after = it != live->tracks.end() ? it->second : BoneTracks{};
        entries.push_back({bone, before, std::move(after)});
    }
    for (const auto& e : entries) {
        const auto sig = [](const BoneTracks& t) {
            std::vector<double> v;
            for (const auto& k : t.translation.keys) v.push_back(k.time);
            v.push_back(-1);
            for (const auto& k : t.rotation.keys) v.push_back(k.time);
            v.push_back(-1);
            for (const auto& k : t.scale.keys) v.push_back(k.time);
            return v;
        };
        if (sig(e.before) != sig(e.after)) changed = true;
    }
    if (!changed) {
        return;
    }
    const char* name = state.mode == KeyDragMode::Duplicate ? "Duplicate Keys"
                       : state.mode == KeyDragMode::Scale   ? "Scale Keys"
                                                            : "Move Keys";
    history_.record(std::make_unique<EditTracksCommand>(state.clipIndex, std::move(entries),
                                                        state.durationBefore, live->duration, name));
    history_.breakMerge();
    emit historyChanged();
}

void Document::copyKeys(const std::vector<KeyRef>& keys) {
    if (const AnimationClip* c = clip()) {
        keyClipboard_ = rigger::copyKeys(*c, keys);
    }
}

std::vector<KeyRef> Document::pasteKeys(double time) {
    std::vector<KeyRef> pasted;
    if (!character_ || keyClipboard_.empty()) {
        return pasted;
    }
    if (clip() == nullptr && !newClip()) {
        return pasted;
    }
    std::vector<BoneId> bones;
    for (const ClipKey& k : keyClipboard_) {
        if (character_->skeleton.contains(k.bone) &&
            std::find(bones.begin(), bones.end(), k.bone) == bones.end()) {
            bones.push_back(k.bone);
        }
    }
    editClipTracks(
        "Paste Keys", {}, bones,
        [&](AnimationClip& work) {
            pasted = rigger::pasteKeys(work, keyClipboard_, time,
                                       [&](BoneId id) { return character_->skeleton.contains(id); });
        },
        true);
    setKeySelection(pasted);
    return pasted;
}

// --- Clips ------------------------------------------------------------------------------------

std::string Document::uniqueClipName(const std::string& base) const {
    std::string name = base;
    for (int i = 2;; ++i) {
        const bool taken = std::any_of(character_->clips.begin(), character_->clips.end(),
                                       [&](const AnimationClip& c) { return c.name == name; });
        if (!taken) {
            return name;
        }
        name = base + " " + std::to_string(i);
    }
}

Result<void> Document::newClip() {
    if (!character_) {
        return Error{"no character"};
    }
    const auto before = character_->clips;
    auto after = before;
    AnimationClip created;
    created.name = uniqueClipName("Clip " + std::to_string(before.size() + 1));
    created.fps = static_cast<float>(defaultFps_);
    created.duration = 1.0;
    after.push_back(created);
    runCommand(std::make_unique<EditClipListCommand>(before, after, "New Clip", created.name));
    clipIndex_ = static_cast<int>(after.size()) - 1;
    time_ = 0.0;
    recomputePose();
    syncPlayer();
    emit clipChanged();
    emit timeChanged();
    emit animationChanged();
    return {};
}

Result<void> Document::duplicateClip() {
    if (clip() == nullptr) {
        return Error{"no clip"};
    }
    const auto before = character_->clips;
    auto after = before;
    AnimationClip copy = *clip();
    copy.name = uniqueClipName(copy.name + " copy");
    after.push_back(copy);
    runCommand(std::make_unique<EditClipListCommand>(before, after, "Duplicate Clip", copy.name));
    clipIndex_ = static_cast<int>(after.size()) - 1;
    recomputePose();
    syncPlayer();
    emit clipChanged();
    emit animationChanged();
    return {};
}

Result<void> Document::renameClip(const QString& name) {
    const AnimationClip* c = clip();
    if (c == nullptr) {
        return Error{"no clip"};
    }
    const std::string trimmed = name.trimmed().toStdString();
    if (trimmed.empty()) {
        return Error{"clip names cannot be empty"};
    }
    ClipMeta before = ClipMeta::of(*c);
    ClipMeta after = before;
    after.name = trimmed;
    runCommand(std::make_unique<EditClipMetaCommand>(static_cast<std::size_t>(clipIndex_), before, after,
                                                     "Rename Clip"));
    emit clipChanged();
    emit animationChanged();
    return {};
}

Result<void> Document::deleteClip() {
    const AnimationClip* c = clip();
    if (c == nullptr) {
        return Error{"no clip"};
    }
    const auto before = character_->clips;
    auto after = before;
    const std::string removed = c->name;
    after.erase(after.begin() + clipIndex_);
    setPlaying(false);
    // Les couches, bandes et entrées du blend tree référencent les clips par indice : on les met à jour (et
    // on retire celles qui pointaient sur le clip supprimé) dans la même entrée d'historique.
    AnimationMix remapped = character_->mix;
    const int gone = clipIndex_;
    const auto fix = [gone](auto& list) {
        list.erase(std::remove_if(list.begin(), list.end(), [gone](const auto& e) { return e.clip == gone; }),
                   list.end());
        for (auto& e : list) {
            if (e.clip > gone) --e.clip;
        }
    };
    fix(remapped.layers);
    fix(remapped.strips);
    fix(remapped.tree.entries);
    history_.beginGroup("Delete Clip");
    runCommand(std::make_unique<EditClipListCommand>(before, after, "Delete Clip", removed));
    if (!(remapped == character_->mix)) {
        runCommand(std::make_unique<EditMixCommand>(character_->mix, remapped, "Delete Clip", removed));
    }
    history_.endGroup();
    clipIndex_ = after.empty() ? -1 : std::min(clipIndex_, static_cast<int>(after.size()) - 1);
    time_ = 0.0;
    setKeySelection({});
    recomputePose();
    syncPlayer();
    emit clipChanged();
    emit timeChanged();
    emit animationChanged();
    return {};
}

bool Document::setClipFps(float fps) {
    const AnimationClip* c = clip();
    if (c == nullptr || fps < 1.0f || fps > 240.0f || fps == c->fps) {
        return false;
    }
    ClipMeta before = ClipMeta::of(*c);
    ClipMeta after = before;
    after.fps = fps;
    runCommand(std::make_unique<EditClipMetaCommand>(static_cast<std::size_t>(clipIndex_), before, after,
                                                     "Edit Clip"));
    emit timeChanged();
    emit animationChanged();
    return true;
}

bool Document::setClipLoop(bool loop) {
    const AnimationClip* c = clip();
    if (c == nullptr || loop == c->loop) {
        return false;
    }
    ClipMeta before = ClipMeta::of(*c);
    ClipMeta after = before;
    after.loop = loop;
    runCommand(std::make_unique<EditClipMetaCommand>(static_cast<std::size_t>(clipIndex_), before, after,
                                                     "Edit Clip"));
    emit animationChanged();
    return true;
}

bool Document::setClipDuration(double seconds) {
    const AnimationClip* c = clip();
    if (c == nullptr || seconds < 0.0 || seconds == c->duration) {
        return false;
    }
    ClipMeta before = ClipMeta::of(*c);
    ClipMeta after = before;
    after.duration = std::max(seconds, lastKeyTime(*c));  // jamais plus court que la dernière clé
    if (after.duration == before.duration) {
        return false;
    }
    runCommand(std::make_unique<EditClipMetaCommand>(static_cast<std::size_t>(clipIndex_), before, after,
                                                     "Edit Clip"));
    syncPlayer();
    emit animationChanged();
    return true;
}

bool Document::addMarker(double time, const QString& name) {
    const AnimationClip* c = clip();
    if (c == nullptr) {
        return false;
    }
    ClipMeta before = ClipMeta::of(*c);
    ClipMeta after = before;
    after.markers.push_back({std::max(time, 0.0), name.toStdString()});
    std::stable_sort(after.markers.begin(), after.markers.end(),
                     [](const Marker& a, const Marker& b) { return a.time < b.time; });
    runCommand(std::make_unique<EditClipMetaCommand>(static_cast<std::size_t>(clipIndex_), before, after,
                                                     "Edit Clip"));
    emit animationChanged();
    return true;
}

bool Document::removeMarker(std::size_t index) {
    const AnimationClip* c = clip();
    if (c == nullptr || index >= c->markers.size()) {
        return false;
    }
    ClipMeta before = ClipMeta::of(*c);
    ClipMeta after = before;
    after.markers.erase(after.markers.begin() + static_cast<std::ptrdiff_t>(index));
    runCommand(std::make_unique<EditClipMetaCommand>(static_cast<std::size_t>(clipIndex_), before, after,
                                                     "Edit Clip"));
    emit animationChanged();
    return true;
}

// --- Bibliothèque de poses ----------------------------------------------------------------------

Result<void> Document::savePose(const QString& name, bool selectedOnly) {
    if (!character_) {
        return Error{"no character"};
    }
    const std::string trimmed = name.trimmed().toStdString();
    if (trimmed.empty()) {
        return Error{"pose names cannot be empty"};
    }
    const Skeleton& skeleton = character_->skeleton;
    const Pose pose = editPose();
    NamedPose entry;
    entry.name = trimmed;
    const std::vector<BoneId> ids =
        selectedOnly && !selection_.empty() ? selection_ : std::vector<BoneId>(skeleton.order());
    for (const BoneId id : ids) {
        if (skeleton.contains(id) && id < pose.size()) {
            entry.bones.emplace_back(skeleton.bone(id).name, pose[id]);
        }
    }
    const auto before = character_->poses;
    auto after = before;
    // Un nom existant est remplacé.
    const auto same =
        std::find_if(after.begin(), after.end(), [&](const NamedPose& p) { return p.name == trimmed; });
    if (same != after.end()) {
        *same = entry;
    } else {
        after.push_back(entry);
    }
    runCommand(std::make_unique<EditPoseLibraryCommand>(before, after, "Save Pose", trimmed));
    emit animationChanged();
    return {};
}

bool Document::applyPose(std::size_t index) {
    if (!character_ || index >= character_->poses.size()) {
        return false;
    }
    std::vector<std::pair<BoneId, Transform>> edits;
    for (const auto& [boneName, transform] : character_->poses[index].bones) {
        const BoneId id = character_->skeleton.find(boneName);
        if (id != kNoBone) {
            edits.emplace_back(id, transform);
        }
    }
    return setBonesLocal(edits, {});
}

bool Document::deletePose(std::size_t index) {
    if (!character_ || index >= character_->poses.size()) {
        return false;
    }
    const auto before = character_->poses;
    auto after = before;
    const std::string removed = after[index].name;
    after.erase(after.begin() + static_cast<std::ptrdiff_t>(index));
    runCommand(std::make_unique<EditPoseLibraryCommand>(before, after, "Delete Pose", removed));
    emit animationChanged();
    return true;
}

}  // namespace rigger::editor
