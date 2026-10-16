// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/command/command.hpp"

namespace rigger {

// Historique annuler / rétablir : deux piles, limite configurable, groupes (transactions atomiques) et fusion
// des commandes continues. Sans Qt : l'éditeur l'enveloppe dans un QUndoStack pour l'affichage.
class History {
public:
    static constexpr std::size_t kDefaultLimit = 200;

    explicit History(std::size_t limit = kDefaultLimit);
    ~History();
    History(const History&) = delete;
    History& operator=(const History&) = delete;

    // Exécute la commande puis l'enregistre (efface la pile « rétablir »).
    void execute(std::unique_ptr<Command> command, EditContext& context);
    // Enregistre une commande DÉJÀ appliquée (glissé interactif : l'état final est déjà à l'écran).
    void record(std::unique_ptr<Command> command);

    bool canUndo() const { return !undo_.empty() && group_ == nullptr; }
    bool canRedo() const { return !redo_.empty() && group_ == nullptr; }
    // Renvoie la commande annulée / rétablie (nullptr si rien à faire) pour que l'appelant rafraîchisse
    // l'état.
    const Command* undo(EditContext& context);
    const Command* redo(EditContext& context);

    // Transactions : toutes les commandes exécutées / enregistrées entre beginGroup et le endGroup
    // correspondant ne forment qu'une entrée d'historique (annulée d'un bloc). Les groupes s'imbriquent.
    void beginGroup(std::string name);
    void endGroup();
    bool inGroup() const { return group_ != nullptr; }

    // La prochaine commande ne fusionnera pas avec la précédente (fin d'un glissé, changement de sélection…).
    void breakMerge() { mergeBarrier_ = true; }

    // Limite du nombre d'entrées conservées (les plus anciennes sont oubliées).
    void setLimit(std::size_t limit);
    std::size_t limit() const { return limit_; }

    std::size_t undoCount() const { return undo_.size(); }
    std::size_t redoCount() const { return redo_.size(); }
    // Entrées dans l'ordre chronologique : les undoCount() premières sont annulables, les suivantes
    // rétablissables.
    const Command& entry(std::size_t index) const;
    std::size_t size() const { return undo_.size() + redo_.size(); }
    const Command* nextUndo() const { return undo_.empty() ? nullptr : undo_.back().get(); }
    const Command* nextRedo() const { return redo_.empty() ? nullptr : redo_.back().get(); }

    // Marqueur « modifié » basé sur l'état de l'historique : vrai si on est revenu exactement à l'état
    // marqué.
    bool isClean() const { return cleanDepth_ == static_cast<long>(undo_.size()); }
    void markClean() { cleanDepth_ = static_cast<long>(undo_.size()); }

    void clear();
    // Incrémenté à chaque changement (entrée ajoutée / fusionnée, undo, redo, clear) : permet de détecter un
    // changement.
    std::uint64_t revision() const { return revision_; }

private:
    class Composite;
    void push(std::unique_ptr<Command> command);
    void trim();

    std::vector<std::unique_ptr<Command>> undo_;  // la dernière est la plus récente
    std::vector<std::unique_ptr<Command>> redo_;  // la dernière est la prochaine à rétablir
    std::size_t limit_;
    long cleanDepth_ = 0;
    bool mergeBarrier_ = true;
    std::uint64_t revision_ = 0;

    // Groupe ouvert (pile de groupes imbriqués).
    std::vector<std::unique_ptr<Composite>> groupStack_;
    Composite* group_ = nullptr;
};

}  // namespace rigger
