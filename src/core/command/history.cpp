// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/command/history.hpp"

#include <algorithm>
#include <cassert>

namespace rigger {

// Groupe de commandes : exécutées dans l'ordre, annulées dans l'ordre inverse.
class History::Composite : public Command {
public:
    explicit Composite(std::string name) : name_(std::move(name)) {}

    void execute(EditContext& context) override {
        for (auto& c : commands_) {
            c->execute(context);
        }
    }
    void undo(EditContext& context) override {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
            (*it)->undo(context);
        }
    }
    std::string name() const override { return name_; }
    std::string detail() const override {
        return commands_.size() == 1 ? commands_.front()->detail() : std::string();
    }
    Effect effects() const override {
        Effect all = Effect::None;
        for (const auto& c : commands_) {
            all = all | c->effects();
        }
        return all;
    }
    std::vector<BoneId> affectedBones() const override {
        std::vector<BoneId> ids;
        for (const auto& c : commands_) {
            for (const BoneId id : c->affectedBones()) {
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                    ids.push_back(id);
                }
            }
        }
        return ids;
    }

    // Ajoute une commande déjà appliquée, en la fusionnant avec la précédente du groupe si possible.
    void add(std::unique_ptr<Command> command, bool allowMerge) {
        if (allowMerge && !commands_.empty() && commands_.back()->merge(*command)) {
            return;
        }
        commands_.push_back(std::move(command));
    }
    bool empty() const { return commands_.empty(); }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};

History::History(std::size_t limit) : limit_(limit) {}

History::~History() = default;

void History::execute(std::unique_ptr<Command> command, EditContext& context) {
    command->execute(context);
    record(std::move(command));
}

void History::record(std::unique_ptr<Command> command) {
    if (group_ != nullptr) {
        group_->add(std::move(command), !mergeBarrier_);
        mergeBarrier_ = false;
        return;
    }
    push(std::move(command));
}

void History::push(std::unique_ptr<Command> command) {
    ++revision_;
    if (!redo_.empty()) {
        // Une nouvelle action écarte l'avenir : si l'état « propre » s'y trouvait, il devient inatteignable.
        if (cleanDepth_ > static_cast<long>(undo_.size())) {
            cleanDepth_ = -1;
        }
        redo_.clear();
        mergeBarrier_ = true;
    }
    if (!mergeBarrier_ && !undo_.empty() && undo_.back()->merge(*command)) {
        if (cleanDepth_ == static_cast<long>(undo_.size())) {
            cleanDepth_ = -1;  // l'entrée « propre » a été modifiée
        }
        return;
    }
    undo_.push_back(std::move(command));
    mergeBarrier_ = false;
    trim();
}

void History::trim() {
    while (undo_.size() > limit_) {
        undo_.erase(undo_.begin());
        if (cleanDepth_ >= 0) {
            --cleanDepth_;
        }
    }
}

const Command* History::undo(EditContext& context) {
    if (!canUndo()) {
        return nullptr;
    }
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    redo_.back()->undo(context);
    mergeBarrier_ = true;
    ++revision_;
    return redo_.back().get();
}

const Command* History::redo(EditContext& context) {
    if (!canRedo()) {
        return nullptr;
    }
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    undo_.back()->execute(context);
    mergeBarrier_ = true;
    ++revision_;
    return undo_.back().get();
}

void History::beginGroup(std::string name) {
    groupStack_.push_back(std::make_unique<Composite>(std::move(name)));
    group_ = groupStack_.back().get();
    mergeBarrier_ = true;
}

void History::endGroup() {
    assert(group_ != nullptr && "endGroup() sans beginGroup()");
    if (group_ == nullptr) {
        return;
    }
    std::unique_ptr<Composite> finished = std::move(groupStack_.back());
    groupStack_.pop_back();
    group_ = groupStack_.empty() ? nullptr : groupStack_.back().get();
    mergeBarrier_ = true;
    if (finished->empty()) {
        return;
    }
    // Déjà appliquée : enregistrée telle quelle dans le groupe parent, ou dans l'historique.
    record(std::move(finished));
    mergeBarrier_ = true;  // un groupe ne fusionne jamais avec la commande suivante
}

void History::setLimit(std::size_t limit) {
    limit_ = std::max<std::size_t>(limit, 1);
    trim();
    ++revision_;
}

const Command& History::entry(std::size_t index) const {
    if (index < undo_.size()) {
        return *undo_[index];
    }
    return *redo_[redo_.size() - 1 - (index - undo_.size())];
}

void History::clear() {
    undo_.clear();
    redo_.clear();
    groupStack_.clear();
    group_ = nullptr;
    cleanDepth_ = 0;
    mergeBarrier_ = true;
    ++revision_;
}

}  // namespace rigger
