// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skeleton/skeleton.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace rigger {

const Bone& Skeleton::bone(BoneId id) const {
    assert(contains(id));
    return slots_[id];
}

BoneId Skeleton::find(std::string_view name) const {
    auto it = byName_.find(std::string(name));
    return it == byName_.end() ? kNoBone : it->second;
}

std::vector<BoneId> Skeleton::children(BoneId id) const {
    std::vector<BoneId> out;
    for (BoneId i = 0; i < slots_.size(); ++i)
        if (alive_[i] && slots_[i].parent == id) out.push_back(i);
    return out;
}

std::vector<BoneId> Skeleton::rootBones() const { return children(kNoBone); }

bool Skeleton::isDescendantOf(BoneId id, BoneId ancestor) const {
    if (!contains(id)) return false;
    BoneId cur = slots_[id].parent;
    for (std::size_t guard = 0; cur != kNoBone && guard <= slots_.size(); ++guard) {
        if (cur == ancestor) return true;
        cur = slots_[cur].parent;
    }
    return false;
}

std::string Skeleton::uniqueName(const std::string& base, BoneId self) const {
    auto free = [&](const std::string& n) {
        auto it = byName_.find(n);
        return it == byName_.end() || it->second == self;
    };
    if (free(base)) return base;
    for (int n = 1;; ++n) {
        char suffix[16];
        std::snprintf(suffix, sizeof suffix, ".%03d", n);
        std::string candidate = base + suffix;
        if (free(candidate)) return candidate;
    }
}

void Skeleton::ensureSlot(BoneId id) {
    if (slots_.size() <= id) {
        slots_.resize(static_cast<std::size_t>(id) + 1);
        alive_.resize(static_cast<std::size_t>(id) + 1, 0);
    }
}

void Skeleton::eraseSlot(BoneId id) {
    byName_.erase(slots_[id].name);
    slots_[id] = Bone{};
    alive_[id] = 0;
    --count_;
}

void Skeleton::restoreFrom(const Skeleton& other) {
    const std::uint64_t next = std::max(revision_, other.revision_) + 1;
    *this = other;
    revision_ = next;
    cacheDirty_ = true;
}

void Skeleton::touch() {
    ++revision_;
    cacheDirty_ = true;
}

void Skeleton::rebuildOrder() {
    std::vector<std::vector<BoneId>> kids(slots_.size());
    std::vector<BoneId> roots;
    for (BoneId i = 0; i < slots_.size(); ++i) {
        if (!alive_[i]) continue;
        if (slots_[i].parent == kNoBone)
            roots.push_back(i);
        else
            kids[slots_[i].parent].push_back(i);
    }
    order_.clear();
    order_.reserve(count_);
    std::vector<BoneId> stack(roots.rbegin(), roots.rend());
    while (!stack.empty()) {
        const BoneId id = stack.back();
        stack.pop_back();
        order_.push_back(id);
        for (auto it = kids[id].rbegin(); it != kids[id].rend(); ++it) stack.push_back(*it);
    }
    assert(order_.size() == count_);
}

Result<BoneId> Skeleton::addBone(std::string name, BoneId parent, const Transform& bindLocal) {
    if (name.empty()) return Error{"bone name must not be empty"};
    if (parent != kNoBone && !contains(parent)) return Error{"parent bone does not exist"};

    const BoneId id = nextId_++;
    ensureSlot(id);
    Bone& b = slots_[id];
    b = Bone{};
    b.id = id;
    b.name = uniqueName(name, kNoBone);
    b.parent = parent;
    b.bindLocal = bindLocal;
    alive_[id] = 1;
    byName_[b.name] = id;
    ++count_;
    order_.push_back(id);  // le parent est déjà dans order_ : l'invariant topologique est conservé
    touch();
    return id;
}

Result<void> Skeleton::removeBone(BoneId id, RemoveMode mode) {
    if (!contains(id)) return Error{"unknown bone"};

    if (mode == RemoveMode::WithDescendants) {
        std::vector<std::uint8_t> doomed(slots_.size(), 0);
        doomed[id] = 1;
        for (BoneId b : order_) {  // parents avant enfants : une seule passe suffit
            const BoneId p = slots_[b].parent;
            if (p != kNoBone && doomed[p]) doomed[b] = 1;
        }
        for (BoneId b = 0; b < doomed.size(); ++b)
            if (doomed[b]) eraseSlot(b);
    } else {
        const Bone removed = slots_[id];
        for (BoneId c : children(id)) {
            slots_[c].parent = removed.parent;
            slots_[c].bindLocal = removed.bindLocal * slots_[c].bindLocal;  // conserve la position monde
        }
        eraseSlot(id);
    }
    rebuildOrder();
    touch();
    return Result<void>{};
}

Result<void> Skeleton::reparent(BoneId id, BoneId newParent, bool keepWorld) {
    if (!contains(id)) return Error{"unknown bone"};
    if (newParent != kNoBone && !contains(newParent)) return Error{"new parent does not exist"};
    if (newParent == id || (newParent != kNoBone && isDescendantOf(newParent, id)))
        return Error{"reparenting would create a cycle"};

    if (keepWorld) {
        const glm::mat4 world = bindGlobals()[id];
        const glm::mat4 parentWorld = newParent == kNoBone ? glm::mat4(1.0f) : bindGlobals()[newParent];
        slots_[id].bindLocal = Transform::fromMat4(glm::inverse(parentWorld) * world);
    }
    slots_[id].parent = newParent;
    rebuildOrder();
    touch();
    return Result<void>{};
}

Result<void> Skeleton::rename(BoneId id, std::string newName) {
    if (!contains(id)) return Error{"unknown bone"};
    if (newName.empty()) return Error{"bone name must not be empty"};
    byName_.erase(slots_[id].name);
    slots_[id].name = uniqueName(newName, id);
    byName_[slots_[id].name] = id;
    touch();
    return Result<void>{};
}

Result<void> Skeleton::restoreBone(const Bone& b) {
    if (b.id == kNoBone) return Error{"invalid bone id"};
    if (contains(b.id)) return Error{"bone id already in use"};
    if (b.name.empty()) return Error{"bone name must not be empty"};
    if (byName_.count(b.name) != 0) return Error{"bone name already in use"};
    if (b.parent != kNoBone && !contains(b.parent)) return Error{"parent bone does not exist"};

    ensureSlot(b.id);
    slots_[b.id] = b;
    alive_[b.id] = 1;
    byName_[b.name] = b.id;
    ++count_;
    nextId_ = std::max<BoneId>(nextId_, b.id + 1);
    rebuildOrder();
    touch();
    return Result<void>{};
}

void Skeleton::setBindLocal(BoneId id, const Transform& bindLocal) {
    assert(contains(id));
    slots_[id].bindLocal = bindLocal;
    touch();
}

void Skeleton::setLength(BoneId id, float length) {
    assert(contains(id));
    slots_[id].length = length;
    touch();
}

void Skeleton::setFlags(BoneId id, BoneFlags flags) {
    assert(contains(id));
    slots_[id].flags = flags;
    touch();
}

void Skeleton::setColor(BoneId id, const glm::vec3& color) {
    assert(contains(id));
    slots_[id].color = color;
    touch();
}

Pose Skeleton::bindPose() const {
    Pose p(slots_.size());
    for (BoneId i = 0; i < slots_.size(); ++i)
        if (alive_[i]) p[i] = slots_[i].bindLocal;
    return p;
}

void Skeleton::ensureCache() const {
    if (!cacheDirty_) return;
    bindGlobals_.assign(slots_.size(), glm::mat4(1.0f));
    inverseBind_.assign(slots_.size(), glm::mat4(1.0f));
    for (BoneId id : order_) {
        const Bone& b = slots_[id];
        const glm::mat4 local = b.bindLocal.toMat4();
        bindGlobals_[id] = b.parent == kNoBone ? local : bindGlobals_[b.parent] * local;
        inverseBind_[id] = glm::inverse(bindGlobals_[id]);
    }
    cacheDirty_ = false;
}

const std::vector<glm::mat4>& Skeleton::bindGlobals() const {
    ensureCache();
    return bindGlobals_;
}

const std::vector<glm::mat4>& Skeleton::inverseBind() const {
    ensureCache();
    return inverseBind_;
}

}  // namespace rigger
