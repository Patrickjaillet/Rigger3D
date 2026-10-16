// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Document : cinématique inverse (Phase 10). Contrôles IK, os de contrôle, bascule IK/FK, bake, verrous de
// pied et préréglage humanoïde. Les solveurs vivent dans core/ik.
#include <QString>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/animation/clip.hpp"
#include "core/ik/rig.hpp"
#include "core/math/quat_utils.hpp"
#include "core/skeleton/fk.hpp"
#include "editor/Document.hpp"

namespace rigger::editor {

namespace {

constexpr double kBlendSeconds = 0.25;

const std::vector<IKControl> kNoControls;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

bool containsAny(const std::string& name, std::initializer_list<const char*> tokens) {
    return std::any_of(tokens.begin(), tokens.end(), [&](const char* t) { return contains(name, t); });
}

// 'L' / 'R' d'après les jetons du nom (« arm.L », « Hand_r », « LeftFoot »), 0 sinon.
char sideOf(const std::string& rawName) {
    const std::string name = lower(rawName);
    std::string token;
    std::vector<std::string> tokens;
    for (const char c : name + ".") {
        if (c == '.' || c == '_' || c == '-' || c == ' ' || std::isdigit(static_cast<unsigned char>(c))) {
            if (!token.empty()) tokens.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    for (const std::string& t : tokens) {
        if (t == "l" || t == "left") return 'L';
        if (t == "r" || t == "right") return 'R';
    }
    if (name.rfind("left", 0) == 0) return 'L';
    if (name.rfind("right", 0) == 0) return 'R';
    return 0;
}

// Os le plus haut dans la hiérarchie dont le nom contient un des jetons, du côté demandé (0 : tous).
BoneId topmost(const Skeleton& sk, std::initializer_list<const char*> tokens, char side) {
    for (const BoneId id : sk.order()) {
        const std::string name = lower(sk.bone(id).name);
        if (!containsAny(name, tokens) || (side != 0 && sideOf(sk.bone(id).name) != side)) continue;
        if (hasFlag(sk.bone(id).flags, BoneFlags::Control)) continue;
        const BoneId parent = sk.bone(id).parent;
        if (parent != kNoBone && containsAny(lower(sk.bone(parent).name), tokens) &&
            (side == 0 || sideOf(sk.bone(parent).name) == side)) {
            continue;
        }
        return id;
    }
    return kNoBone;
}

// Descend la première branche jusqu'à un os terminal (main, poignet, pied...) ou une feuille.
BoneId walkToEnd(const Skeleton& sk, BoneId start, std::initializer_list<const char*> terminal,
                 int maxSteps = 8) {
    BoneId cur = start;
    for (int i = 0; i < maxSteps; ++i) {
        if (cur != start && containsAny(lower(sk.bone(cur).name), terminal)) return cur;
        const auto children = sk.children(cur);
        if (children.empty()) return cur;
        cur = children.front();
    }
    return cur;
}

Transform controlTransform(const std::vector<glm::mat4>& bindWorld, BoneId from) {
    Transform t = Transform::fromMat4(bindWorld[from]);
    t.s = glm::vec3(1.0f);
    return t;
}

BoneId addControlBone(Skeleton& sk, const std::string& name, const Transform& world, const glm::vec3& color) {
    Transform local;
    local.t = world.t;
    local.r = world.r;
    const auto id = sk.addBone(name, kNoBone, local);
    if (!id) return kNoBone;
    sk.setLength(id.value(), 0.1f);
    sk.setColor(id.value(), color);
    sk.setFlags(id.value(), BoneFlags::Control);
    return id.value();
}

}  // namespace

const std::vector<IKControl>& Document::ikControls() const {
    return character_ ? character_->ikControls : kNoControls;
}

int Document::ikControlFor(BoneId bone) const {
    const auto& controls = ikControls();
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const IKControl& c = controls[i];
        if (bone == c.targetBone || bone == c.poleBone || bone == c.chain.effector ||
            std::find(c.chain.bones.begin(), c.chain.bones.end(), bone) != c.chain.bones.end()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Document::commitIK(std::vector<IKControl> after, const char* name, const std::string& detail,
                        const std::string& mergeKey) {
    if (!character_ || after == character_->ikControls) {
        return false;
    }
    runCommand(std::make_unique<EditIKControlsCommand>(character_->ikControls, std::move(after), name, detail,
                                                       mergeKey));
    return true;
}

Result<int> Document::createIK(BoneId effector, int boneCount, IKSolver solver, bool withPole,
                               const glm::vec3& poleDirection, float poleDistance) {
    if (!character_ || !character_->skeleton.contains(effector)) {
        return Error{"unknown bone"};
    }
    int created = -1;
    auto result = editStructure("Create IK", [&]() -> Result<void> {
        Skeleton& sk = character_->skeleton;
        IKChain chain = makeChain(sk, effector, boneCount, solver);
        if (chain.bones.empty()) {
            return Error{"not enough parent bones for this IK chain"};
        }
        if (solver == IKSolver::TwoBone && chain.bones.size() != 2) {
            return Error{"a two-bone IK needs exactly two bones"};
        }
        const auto bindWorld = computeGlobals(sk, sk.bindPose());
        const std::string baseName = sk.bone(effector).name;
        IKControl control;
        control.name = baseName;
        control.chain = chain;
        control.chain.effectorOrientation = EffectorOrientation::MatchTarget;
        control.targetBone =
            addControlBone(sk, "IK_" + baseName, controlTransform(bindWorld, effector), {1.0f, 0.3f, 0.9f});
        if (control.targetBone == kNoBone) {
            return Error{"could not create the target bone"};
        }
        if (withPole && chain.bones.size() == 2) {
            Transform pole = controlTransform(bindWorld, chain.bones[1]);
            pole.t += glm::normalize(poleDirection) * poleDistance;
            pole.r = glm::quat(1, 0, 0, 0);
            control.poleBone = addControlBone(sk, "Pole_" + baseName, pole, {0.3f, 1.0f, 0.5f});
        }
        character_->ikControls.push_back(control);
        created = static_cast<int>(character_->ikControls.size()) - 1;
        recomputePose();
        emit hierarchyChanged();
        emit poseChanged();
        emit ikChanged();
        return {};
    });
    if (!result) {
        return result.error();
    }
    return created;
}

Result<int> Document::createLookAt(BoneId bone, float distance) {
    if (!character_ || !character_->skeleton.contains(bone)) {
        return Error{"unknown bone"};
    }
    int created = -1;
    auto result = editStructure("Create Look-At", [&]() -> Result<void> {
        Skeleton& sk = character_->skeleton;
        const auto bindWorld = computeGlobals(sk, sk.bindPose());
        Transform world = controlTransform(bindWorld, bone);
        world.t += glm::vec3(0.0f, 0.0f, distance);
        world.r = glm::quat(1, 0, 0, 0);
        IKControl control;
        control.name = sk.bone(bone).name;
        control.kind = IKKind::LookAt;
        control.chain.bones = {bone};
        control.targetBone = addControlBone(sk, "Look_" + control.name, world, {1.0f, 0.8f, 0.2f});
        if (control.targetBone == kNoBone) {
            return Error{"could not create the target bone"};
        }
        character_->ikControls.push_back(control);
        created = static_cast<int>(character_->ikControls.size()) - 1;
        recomputePose();
        emit hierarchyChanged();
        emit poseChanged();
        emit ikChanged();
        return {};
    });
    if (!result) {
        return result.error();
    }
    return created;
}

bool Document::editIK(int index, const std::function<void(IKControl&)>& modify, const char* name,
                      const std::string& mergeKey) {
    if (!character_ || index < 0 || static_cast<std::size_t>(index) >= character_->ikControls.size()) {
        return false;
    }
    auto after = character_->ikControls;
    modify(after[static_cast<std::size_t>(index)]);
    const std::string detail = after[static_cast<std::size_t>(index)].name;
    return commitIK(std::move(after), name, detail,
                    mergeKey.empty() ? std::string() : mergeKey + ":" + std::to_string(index));
}

bool Document::removeIK(int index) {
    if (!character_ || index < 0 || static_cast<std::size_t>(index) >= character_->ikControls.size()) {
        return false;
    }
    auto after = character_->ikControls;
    const std::string name = after[static_cast<std::size_t>(index)].name;
    after.erase(after.begin() + index);
    return commitIK(std::move(after), "Remove IK", name);
}

void Document::toggleIK(int index, bool animated) {
    if (!character_ || index < 0 || static_cast<std::size_t>(index) >= character_->ikControls.size() ||
        ikBlend_.active) {
        return;
    }
    const IKControl& control = character_->ikControls[static_cast<std::size_t>(index)];
    const float target = (control.enabled && control.weight > 0.5f) ? 0.0f : 1.0f;
    if (!animated) {
        editIK(
            index,
            [&](IKControl& c) {
                c.enabled = true;
                c.weight = target;
            },
            "Toggle IK");
        return;
    }
    ikBlend_ =
        IKBlend{true, index, control.enabled ? control.weight : 0.0f, target, 0.0, character_->ikControls};
    character_->ikControls[static_cast<std::size_t>(index)].enabled = true;
    connect(&ikTimer_, &QTimer::timeout, this, &Document::tickIKBlend, Qt::UniqueConnection);
    ikTimer_.start(16);
}

void Document::tickIKBlend() {
    if (!ikBlend_.active || !character_ ||
        static_cast<std::size_t>(ikBlend_.index) >= character_->ikControls.size()) {
        ikTimer_.stop();
        ikBlend_ = IKBlend{};
        return;
    }
    ikBlend_.elapsed += 0.016;
    const float u = static_cast<float>(std::min(ikBlend_.elapsed / kBlendSeconds, 1.0));
    const float eased = u * u * (3.0f - 2.0f * u);  // smoothstep
    IKControl& control = character_->ikControls[static_cast<std::size_t>(ikBlend_.index)];
    control.weight = ikBlend_.from + (ikBlend_.to - ikBlend_.from) * eased;
    if (u < 1.0f) {
        emit poseChanged();
        return;
    }
    // Fin de la transition : l'état final est enregistré en UNE entrée d'historique (l'état de départ revient
    // à l'undo).
    ikTimer_.stop();
    auto finalState = character_->ikControls;
    const auto before = ikBlend_.before;
    const std::string name = control.name;
    ikBlend_ = IKBlend{};
    character_->ikControls = before;
    commitIK(std::move(finalState), "Toggle IK", name);
}

Result<void> Document::bakeIK(int index, int firstFrame, int lastFrame, bool disableAfter) {
    AnimationClip* live = mutableClip();
    if (!character_ || live == nullptr) {
        return Error{"there is no clip to bake into"};
    }
    if (index < 0 || static_cast<std::size_t>(index) >= character_->ikControls.size() ||
        lastFrame < firstFrame) {
        return Error{"invalid IK control or frame range"};
    }
    const IKControl control = character_->ikControls[static_cast<std::size_t>(index)];
    std::vector<BoneId> bones = control.chain.bones;
    if (control.kind == IKKind::Chain && control.chain.effectorOrientation != EffectorOrientation::Follow) {
        bones.push_back(control.chain.effector);
    }
    const std::vector<IKControl> controls = character_->ikControls;
    const Skeleton& skeleton = character_->skeleton;
    const double fps = live->fps;
    history_.beginGroup("Bake IK");
    editClipTracks(
        "Bake IK", {}, bones,
        [&](AnimationClip& work) {
            struct Sample {
                BoneId id;
                double time;
                glm::quat rotation;
            };
            std::vector<Sample> samples;
            for (int frame = firstFrame; frame <= lastFrame; ++frame) {
                const double t = frame / fps;
                Pose p = samplePose(*live, skeleton, t);  // `work` ne contient que les pistes de la chaîne
                applyIKControls(skeleton, p, controls, t);
                for (const BoneId id : bones) samples.push_back({id, t, p[id].r});
            }
            for (const Sample& s : samples) {
                Keyframe<glm::quat> key;
                key.time = s.time;
                key.value = s.rotation;
                key.mode = Interpolation::Linear;
                insertKey(work.tracks[s.id].rotation, key);
            }
            extendDuration(work);
        },
        true);
    if (disableAfter) {
        editIK(index, [](IKControl& c) { c.enabled = false; }, "Bake IK");
    }
    history_.endGroup();
    return {};
}

Result<void> Document::addFootLock(int index, int firstFrame, int lastFrame) {
    if (!character_ || index < 0 || static_cast<std::size_t>(index) >= character_->ikControls.size() ||
        lastFrame < firstFrame) {
        return Error{"invalid IK control or frame range"};
    }
    const AnimationClip* c = clip();
    const double fps = c != nullptr ? c->fps : defaultFps_;
    const IKControl& control = character_->ikControls[static_cast<std::size_t>(index)];
    // Position monde de la cible au début du verrou (pose FK de cette image : clip + retouches du moment).
    Pose fk = c != nullptr ? samplePose(*c, character_->skeleton, firstFrame / fps) : pose_;
    const auto globals = computeGlobals(character_->skeleton, fk);
    FootLock lock;
    lock.start = firstFrame / fps;
    lock.end = lastFrame / fps;
    lock.world = Transform::fromMat4(globals[control.targetBone]);
    editIK(index, [&](IKControl& ctl) { ctl.locks.push_back(lock); }, "Add Foot Lock");
    return {};
}

bool Document::removeFootLock(int index, int lockIndex) {
    return editIK(
        index,
        [&](IKControl& c) {
            if (lockIndex >= 0 && static_cast<std::size_t>(lockIndex) < c.locks.size()) {
                c.locks.erase(c.locks.begin() + lockIndex);
            }
        },
        "Remove Foot Lock");
}

Result<int> Document::createHumanoidIK() {
    if (!character_) {
        return Error{"no character"};
    }
    const Skeleton& sk = character_->skeleton;
    int made = 0;
    history_.beginGroup("Humanoid IK");
    for (const char side : {'L', 'R'}) {
        // Bras : de l'épaule / bras à la main, deux os sous la main.
        const BoneId arm = topmost(sk, {"upperarm", "arm", "shoulder", "clavicle"}, side);
        if (arm != kNoBone) {
            const BoneId hand = walkToEnd(sk, arm, {"hand", "wrist"});
            if (hand != arm && createIK(hand, 2, IKSolver::TwoBone, true, {0.0f, 0.0f, -1.0f})) ++made;
        }
        const BoneId leg = topmost(sk, {"thigh", "upleg", "upperleg", "leg"}, side);
        if (leg != kNoBone) {
            const BoneId foot = walkToEnd(sk, leg, {"foot", "ankle"});
            if (foot != leg && createIK(foot, 2, IKSolver::TwoBone, true, {0.0f, 0.0f, 1.0f})) ++made;
        }
    }
    const BoneId spine = topmost(sk, {"spine", "torso", "chest"}, 0);
    if (spine != kNoBone) {
        const BoneId end = walkToEnd(sk, spine, {"neck", "head"});
        int count = 0;
        for (BoneId cur = end; cur != spine && cur != kNoBone; cur = sk.bone(cur).parent) ++count;
        if (end != spine && count >= 2 && createIK(end, count, IKSolver::FABRIK, false)) ++made;
    }
    const BoneId head = topmost(sk, {"head"}, 0);
    if (head != kNoBone && createLookAt(head, 1.0f)) ++made;
    history_.endGroup();
    if (made == 0) {
        return Error{
            "no humanoid bones were recognised (looking for arm / hand, leg / foot, spine and head)"};
    }
    return made;
}

}  // namespace rigger::editor
