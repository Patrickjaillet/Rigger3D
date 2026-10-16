// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include <algorithm>
#include <string>

#include "core/character/edit.hpp"
#include "core/command/commands.hpp"
#include "core/command/history.hpp"
#include "core/skeleton/fk.hpp"
#include "test_helpers.hpp"

using namespace rigger;
using testutil::Rng;
using Entry = SetBoneTransformsCommand::Entry;
using Target = SetBoneTransformsCommand::Target;

namespace rigger {
inline bool operator==(const SkinWeight& a, const SkinWeight& b) {
    return a.joints == b.joints && a.weights == b.weights;
}
}  // namespace rigger

namespace {

// root -> A -> B, root -> C ; un maillage de 4 sommets skinnés ; un clip animant B.
Character makeCharacter() {
    Character c;
    Transform up;
    up.t = {0, 1, 0};
    const BoneId root = c.skeleton.addBone("root", kNoBone).value();
    const BoneId a = c.skeleton.addBone("A", root, up).value();
    const BoneId b = c.skeleton.addBone("B", a, up).value();
    const BoneId cc = c.skeleton.addBone("C", root, up).value();
    Mesh mesh;
    mesh.vertices.resize(4);
    mesh.skinWeights.resize(4);
    const BoneId ids[4] = {root, a, b, cc};
    for (int i = 0; i < 4; ++i) {
        mesh.skinWeights[static_cast<std::size_t>(i)].joints = {ids[i], ids[(i + 1) % 4], kNoBone, kNoBone};
        mesh.skinWeights[static_cast<std::size_t>(i)].weights = {0.6f, 0.4f, 0, 0};
    }
    c.meshes.push_back(mesh);
    AnimationClip clip;
    clip.tracks[b];
    clip.tracks[a];
    c.clips.push_back(clip);
    return c;
}

// Comparaison structurelle de l'état éditable (squelette, poids, pistes, pose).
struct Snapshot {
    struct BoneData {
        BoneId id;
        std::string name;
        BoneId parent;
        Transform bind;
        float length;
        BoneFlags flags;
        glm::vec3 color;
        bool operator==(const BoneData& o) const {
            return id == o.id && name == o.name && parent == o.parent && bind.t == o.bind.t &&
                   bind.r == o.bind.r && bind.s == o.bind.s && length == o.length && flags == o.flags &&
                   color == o.color;
        }
    };
    std::vector<BoneData> bones;
    std::size_t slots = 0;
    std::vector<std::vector<SkinWeight>> weights;
    std::vector<std::size_t> trackBones;
    Pose pose;

    static bool sameWeight(const SkinWeight& a, const SkinWeight& b) {
        return a.joints == b.joints && a.weights == b.weights;
    }

    bool operator==(const Snapshot& o) const {
        if (bones != o.bones || slots != o.slots || trackBones != o.trackBones ||
            weights.size() != o.weights.size() || pose.size() != o.pose.size()) {
            return false;
        }
        for (std::size_t m = 0; m < weights.size(); ++m) {
            if (weights[m].size() != o.weights[m].size() ||
                !std::equal(weights[m].begin(), weights[m].end(), o.weights[m].begin(), sameWeight)) {
                return false;
            }
        }
        for (std::size_t i = 0; i < pose.size(); ++i) {
            if (!(pose[i].t == o.pose[i].t && pose[i].r == o.pose[i].r && pose[i].s == o.pose[i].s)) {
                return false;
            }
        }
        return true;
    }
};

Snapshot snapshot(const Character& c, const Pose& pose) {
    Snapshot s;
    for (const BoneId id : c.skeleton.order()) {
        const Bone& b = c.skeleton.bone(id);
        s.bones.push_back({id, b.name, b.parent, b.bindLocal, b.length, b.flags, b.color});
    }
    s.slots = c.skeleton.slotCount();
    for (const Mesh& m : c.meshes) s.weights.push_back(m.skinWeights);
    for (const AnimationClip& clip : c.clips) {
        std::vector<BoneId> ids;
        for (const auto& [id, tracks] : clip.tracks) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        s.trackBones.push_back(ids.size());
        for (const BoneId id : ids) s.trackBones.push_back(id);
    }
    s.pose = pose;
    return s;
}

struct World {
    Character character = makeCharacter();
    Pose pose = character.skeleton.bindPose();
    EditContext context{&character, &pose};
    History history{10000};
};

void syncPose(World& w) {
    const std::size_t old = w.pose.size();
    w.pose.resize(w.character.skeleton.slotCount());
    for (BoneId id = static_cast<BoneId>(old); id < w.pose.size(); ++id) {
        if (w.character.skeleton.contains(id)) w.pose[id] = w.character.skeleton.bone(id).bindLocal;
    }
}

std::unique_ptr<Command> moveBone(World& w, BoneId id, const glm::vec3& t, Target target,
                                  const std::string& key = {}) {
    const Transform before = target == Target::Bind ? w.character.skeleton.bone(id).bindLocal : w.pose[id];
    Transform after = before;
    after.t = t;
    return std::make_unique<SetBoneTransformsCommand>(target, std::vector<Entry>{{id, before, after}},
                                                      "Move Bone", key);
}

BoneId bone(const World& w, const char* name) { return w.character.skeleton.find(name); }

}  // namespace

TEST_CASE("historique : execute / undo / redo restaurent l'état exact (bind et pose)") {
    World w;
    const Snapshot initial = snapshot(w.character, w.pose);
    const BoneId a = bone(w, "A");

    w.history.execute(moveBone(w, a, {1, 2, 3}, Target::Bind), w.context);
    w.history.execute(moveBone(w, a, {4, 5, 6}, Target::Pose), w.context);
    CHECK(w.character.skeleton.bone(a).bindLocal.t == glm::vec3(1, 2, 3));
    CHECK(w.pose[a].t == glm::vec3(4, 5, 6));
    CHECK(w.history.undoCount() == 2);
    const Snapshot done = snapshot(w.character, w.pose);

    const Command* undone = w.history.undo(w.context);
    REQUIRE(undone != nullptr);
    CHECK(undone->name() == "Move Bone");
    CHECK(undone->effects() == Effect::Transforms);
    CHECK(undone->affectedBones() == std::vector<BoneId>{a});
    CHECK(w.pose[a].t != glm::vec3(4, 5, 6));
    w.history.undo(w.context);
    CHECK(snapshot(w.character, w.pose) == initial);
    CHECK_FALSE(w.history.canUndo());
    CHECK(w.history.undo(w.context) == nullptr);

    w.history.redo(w.context);
    w.history.redo(w.context);
    CHECK(snapshot(w.character, w.pose) == done);
    CHECK_FALSE(w.history.canRedo());
}

TEST_CASE("historique : une nouvelle commande efface la pile rétablir") {
    World w;
    const BoneId a = bone(w, "A");
    w.history.execute(moveBone(w, a, {1, 0, 0}, Target::Bind), w.context);
    w.history.execute(moveBone(w, a, {2, 0, 0}, Target::Bind), w.context);
    w.history.undo(w.context);
    CHECK(w.history.canRedo());
    w.history.execute(moveBone(w, a, {9, 0, 0}, Target::Bind), w.context);
    CHECK_FALSE(w.history.canRedo());
    CHECK(w.history.undoCount() == 2);
}

TEST_CASE("fusion : un glissé = une entrée ; breakMerge, clé ou cible différente séparent") {
    World w;
    const BoneId a = bone(w, "A");
    const Snapshot initial = snapshot(w.character, w.pose);
    for (int i = 1; i <= 50; ++i) {  // curseur d'inspecteur : 50 mises à jour continues
        w.history.execute(moveBone(w, a, {static_cast<float>(i), 0, 0}, Target::Bind, "inspector"),
                          w.context);
    }
    CHECK(w.history.undoCount() == 1);
    CHECK(w.character.skeleton.bone(a).bindLocal.t.x == 50.0f);
    w.history.undo(w.context);
    CHECK(snapshot(w.character, w.pose) == initial);  // retour à l'état AVANT le glissé
    w.history.redo(w.context);
    CHECK(w.character.skeleton.bone(a).bindLocal.t.x == 50.0f);

    w.history.breakMerge();
    w.history.execute(moveBone(w, a, {60, 0, 0}, Target::Bind, "inspector"), w.context);
    CHECK(w.history.undoCount() == 2);
    w.history.execute(moveBone(w, a, {61, 0, 0}, Target::Bind, "autre clé"), w.context);
    CHECK(w.history.undoCount() == 3);
    w.history.execute(moveBone(w, a, {62, 0, 0}, Target::Pose, "autre clé"), w.context);  // autre cible
    CHECK(w.history.undoCount() == 4);
    w.history.execute(moveBone(w, a, {63, 0, 0}, Target::Pose), w.context);  // sans clé : jamais fusionnée
    w.history.execute(moveBone(w, a, {64, 0, 0}, Target::Pose), w.context);
    CHECK(w.history.undoCount() == 6);
    // undo / redo ne fusionnent pas avec la suite.
    w.history.undo(w.context);
    w.history.execute(moveBone(w, a, {70, 0, 0}, Target::Pose, "autre clé"), w.context);
    CHECK(w.history.undoCount() == 6);
}

TEST_CASE("groupes : une seule entrée, annulée d'un bloc, imbrication, groupe vide ignoré") {
    World w;
    const BoneId a = bone(w, "A"), b = bone(w, "B");
    const Snapshot initial = snapshot(w.character, w.pose);

    w.history.beginGroup("Pose Arm");
    CHECK(w.history.inGroup());
    w.history.execute(moveBone(w, a, {1, 0, 0}, Target::Bind), w.context);
    w.history.beginGroup("Inner");
    w.history.execute(moveBone(w, b, {2, 0, 0}, Target::Bind), w.context);
    w.history.execute(moveBone(w, b, {3, 0, 0}, Target::Bind), w.context);
    w.history.endGroup();
    CHECK_FALSE(w.history.canUndo());  // un groupe est ouvert : rien d'annulable à moitié
    w.history.endGroup();
    CHECK_FALSE(w.history.inGroup());
    CHECK(w.history.undoCount() == 1);
    CHECK(w.history.nextUndo()->name() == "Pose Arm");
    CHECK(w.history.nextUndo()->affectedBones().size() == 2);

    w.history.undo(w.context);
    CHECK(snapshot(w.character, w.pose) == initial);
    w.history.redo(w.context);
    CHECK(w.character.skeleton.bone(b).bindLocal.t.x == 3.0f);

    w.history.beginGroup("Vide");
    w.history.endGroup();
    CHECK(w.history.undoCount() == 1);
    // Le groupe ne fusionne pas avec la commande suivante.
    w.history.execute(moveBone(w, a, {5, 0, 0}, Target::Bind, "k"), w.context);
    CHECK(w.history.undoCount() == 2);
}

TEST_CASE("limite : les entrées les plus anciennes sont oubliées") {
    World w;
    const BoneId a = bone(w, "A");
    w.history.setLimit(5);
    for (int i = 1; i <= 12; ++i) {
        w.history.execute(moveBone(w, a, {static_cast<float>(i), 0, 0}, Target::Bind), w.context);
    }
    CHECK(w.history.undoCount() == 5);
    for (int i = 0; i < 10; ++i) w.history.undo(w.context);
    CHECK(w.character.skeleton.bone(a).bindLocal.t.x ==
          7.0f);  // 12 - 5 : l'état avant l'entrée la plus ancienne
    w.history.setLimit(2);
    CHECK(w.history.undoCount() == 0);  // le redo reste, l'undo a été vidé par les undo ci-dessus
    History fresh;
    CHECK(fresh.limit() == History::kDefaultLimit);  // 200 par défaut
    CHECK(History::kDefaultLimit == 200);
}

TEST_CASE("état propre : marqueur « modifié » basé sur la position dans l'historique") {
    World w;
    const BoneId a = bone(w, "A");
    CHECK(w.history.isClean());
    w.history.execute(moveBone(w, a, {1, 0, 0}, Target::Bind), w.context);
    CHECK_FALSE(w.history.isClean());
    w.history.markClean();
    CHECK(w.history.isClean());
    w.history.execute(moveBone(w, a, {2, 0, 0}, Target::Bind), w.context);
    CHECK_FALSE(w.history.isClean());
    w.history.undo(w.context);
    CHECK(w.history.isClean());  // revenu exactement à l'état sauvé
    w.history.undo(w.context);
    CHECK_FALSE(w.history.isClean());
    w.history.redo(w.context);
    CHECK(w.history.isClean());

    // Une nouvelle action après avoir dépassé l'état propre le rend inatteignable.
    w.history.undo(w.context);  // avant l'état propre
    w.history.execute(moveBone(w, a, {7, 0, 0}, Target::Bind), w.context);
    w.history.undo(w.context);
    CHECK_FALSE(w.history.isClean());
    w.history.redo(w.context);
    CHECK_FALSE(w.history.isClean());

    // Fusionner dans l'entrée propre la salit.
    World m;
    const BoneId ma = bone(m, "A");
    m.history.execute(moveBone(m, ma, {1, 0, 0}, Target::Bind, "k"), m.context);
    m.history.markClean();
    m.history.execute(moveBone(m, ma, {2, 0, 0}, Target::Bind, "k"), m.context);  // fusionnée
    CHECK(m.history.undoCount() == 1);
    CHECK_FALSE(m.history.isClean());
    m.history.undo(m.context);
    CHECK_FALSE(m.history.isClean());  // l'état marqué n'existe plus
    m.history.clear();
    CHECK(m.history.isClean());
}

TEST_CASE("propriétés d'os : renommage, longueur, couleur, drapeaux annulables") {
    World w;
    const BoneId a = bone(w, "A");
    const Snapshot initial = snapshot(w.character, w.pose);
    BoneProps after = BoneProps::of(w.character.skeleton.bone(a));
    after.name = "Elbow";
    after.length = 0.7f;
    after.color = {1, 0, 0};
    after.flags = BoneFlags::Hidden | BoneFlags::Locked;
    w.history.execute(std::make_unique<SetBonePropertiesCommand>(
                          std::vector<SetBonePropertiesCommand::Entry>{
                              {a, BoneProps::of(w.character.skeleton.bone(a)), after}},
                          "Rename Bone"),
                      w.context);
    CHECK(w.character.skeleton.bone(a).name == "Elbow");
    CHECK(w.character.skeleton.find("A") == kNoBone);
    CHECK(w.history.nextUndo()->detail() == "Elbow");
    CHECK(w.history.nextUndo()->effects() == Effect::Properties);
    w.history.undo(w.context);
    CHECK(snapshot(w.character, w.pose) == initial);
    CHECK(w.character.skeleton.find("A") == a);  // l'index par nom suit
}

TEST_CASE("structure : ajout / suppression / subdivision restaurent squelette, poids et pistes") {
    World w;
    const Snapshot initial = snapshot(w.character, w.pose);
    const auto record = [&](const char* name, const auto& edit) {
        StructureState before = StructureState::capture(w.character);
        edit();
        syncPose(w);
        w.history.record(std::make_unique<StructureCommand>(name, "", std::move(before),
                                                            StructureState::capture(w.character)));
    };
    record("Add Bone", [&] { w.character.skeleton.addBone("New", bone(w, "B")); });
    const Snapshot afterAdd = snapshot(w.character, w.pose);
    record("Delete Bone", [&] { removeBoneClean(w.character, bone(w, "A"), RemoveMode::ReparentChildren); });
    const Snapshot afterDelete = snapshot(w.character, w.pose);
    CHECK(afterDelete.weights != initial.weights);  // les poids ont été reportés
    record("Subdivide Bone", [&] { subdivideBone(w.character, bone(w, "C")); });
    const Snapshot afterAll = snapshot(w.character, w.pose);

    w.history.undo(w.context);
    CHECK(snapshot(w.character, w.pose) == afterDelete);
    w.history.undo(w.context);
    CHECK(snapshot(w.character, w.pose) == afterAdd);
    CHECK(w.character.skeleton.find("A") != kNoBone);  // os restauré avec son identifiant d'origine
    w.history.undo(w.context);
    CHECK(snapshot(w.character, w.pose) == initial);
    w.history.redo(w.context);
    w.history.redo(w.context);
    w.history.redo(w.context);
    CHECK(snapshot(w.character, w.pose) == afterAll);
    CHECK(w.history.nextUndo()->effects() ==
          (Effect::Structure | Effect::Weights | Effect::Transforms | Effect::IK));
}

TEST_CASE("fuzzing : 1000 opérations aléatoires, tout annuler = état initial, tout rétablir = état final") {
    for (const unsigned seed : {1u, 2u, 3u}) {
        Rng rng(seed);
        World w;
        const Snapshot initial = snapshot(w.character, w.pose);
        const auto pickBone = [&]() {
            const auto& order = w.character.skeleton.order();
            return order[static_cast<std::size_t>(rng.integer(0, static_cast<int>(order.size()) - 1))];
        };
        const auto recordStructure = [&](const char* name, const auto& edit) {
            StructureState before = StructureState::capture(w.character);
            edit();
            syncPose(w);
            w.history.record(std::make_unique<StructureCommand>(name, "", std::move(before),
                                                                StructureState::capture(w.character)));
        };

        int structureOps = 0;
        for (int i = 0; i < 1000; ++i) {
            int choice = rng.integer(0, 11);
            if (w.character.skeleton.boneCount() < 4) choice = 8;  // garde un squelette fourni
            if (choice <= 2) {
                w.history.execute(
                    moveBone(w, pickBone(), rng.vec(2.0f), Target::Bind, choice == 0 ? "drag" : ""),
                    w.context);
            } else if (choice <= 4) {
                const BoneId id = pickBone();
                if (id < w.pose.size()) {
                    w.history.execute(moveBone(w, id, rng.vec(2.0f), Target::Pose, choice == 3 ? "drag" : ""),
                                      w.context);
                }
            } else if (choice == 5) {
                const BoneId id = pickBone();
                BoneProps after = BoneProps::of(w.character.skeleton.bone(id));
                after.name = "bone_" + std::to_string(rng.integer(0, 99999));
                after.length = rng.uniform(0.05f, 2.0f);
                after.color = glm::abs(rng.vec(1.0f));
                after.flags = rng.integer(0, 1) != 0 ? BoneFlags::Hidden : BoneFlags::None;
                w.history.execute(std::make_unique<SetBonePropertiesCommand>(
                                      std::vector<SetBonePropertiesCommand::Entry>{
                                          {id, BoneProps::of(w.character.skeleton.bone(id)), after}},
                                      "Edit Bone"),
                                  w.context);
            } else if (choice == 6) {
                w.history.beginGroup("Group");
                for (int k = 0; k < 3; ++k) {
                    w.history.execute(moveBone(w, pickBone(), rng.vec(2.0f), Target::Bind), w.context);
                }
                w.history.endGroup();
            } else if (choice == 7) {
                w.history.breakMerge();
            } else if (choice == 8) {
                ++structureOps;
                recordStructure("Add Bone", [&] {
                    Transform t;
                    t.t = rng.vec(1.0f);
                    w.character.skeleton.addBone("n" + std::to_string(i), pickBone(), t);
                });
            } else if (choice == 9) {
                ++structureOps;
                recordStructure("Delete Bone", [&] {
                    removeBoneClean(
                        w.character, pickBone(),
                        rng.integer(0, 1) != 0 ? RemoveMode::WithDescendants : RemoveMode::ReparentChildren);
                });
            } else if (choice == 10) {
                ++structureOps;
                recordStructure("Subdivide Bone", [&] { subdivideBone(w.character, pickBone()); });
            } else {
                // Annuler / rétablir au hasard au milieu de la séquence.
                const int steps = rng.integer(1, 4);
                for (int k = 0; k < steps; ++k) {
                    if (rng.integer(0, 1) != 0)
                        w.history.undo(w.context);
                    else
                        w.history.redo(w.context);
                }
            }
            if (w.character.skeleton.boneCount() == 0) {
                break;
            }
        }
        CHECK(structureOps > 50);  // la séquence exerce bien les éditions structurelles
        const Snapshot final = snapshot(w.character, w.pose);
        const Snapshot trueInitial = initial;

        // Rejoue d'abord tout l'avenir (redo) pour se placer sur l'état final de la pile, puis annule tout.
        while (w.history.redo(w.context) != nullptr) {
        }
        const Snapshot top = snapshot(w.character, w.pose);
        std::size_t undone = 0;
        while (w.history.undo(w.context) != nullptr) ++undone;
        CHECK(undone > 0);
        CHECK(snapshot(w.character, w.pose) == trueInitial);
        std::size_t redone = 0;
        while (w.history.redo(w.context) != nullptr) ++redone;
        CHECK(redone == undone);
        CHECK(snapshot(w.character, w.pose) == top);
        (void) final;
        for (const Mesh& mesh : w.character.meshes) {
            CHECK(mesh.validate().ok());
        }
    }
}
