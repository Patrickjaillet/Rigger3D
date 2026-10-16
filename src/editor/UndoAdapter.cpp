// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/UndoAdapter.hpp"

#include <QUndoCommand>
#include <QUndoStack>

#include "core/command/history.hpp"
#include "editor/CommandNames.hpp"

namespace rigger::editor {

class UndoAdapter::Mirror : public QUndoCommand {
public:
    Mirror(UndoAdapter& adapter, const QString& text) : QUndoCommand(text), adapter_(adapter) {}
    void redo() override { forward(adapter_.redo_); }
    void undo() override { forward(adapter_.undo_); }

private:
    void forward(const std::function<void()>& handler) {
        if (adapter_.syncing_ || !handler) {
            return;
        }
        adapter_.inStackCall_ = true;
        handler();
        adapter_.inStackCall_ = false;
    }
    UndoAdapter& adapter_;
};

UndoAdapter::UndoAdapter(History& history, QObject* parent)
    : QObject(parent), history_(history), stack_(new QUndoStack(this)) {}

void UndoAdapter::setHandlers(std::function<void()> undo, std::function<void()> redo) {
    undo_ = std::move(undo);
    redo_ = std::move(redo);
}

void UndoAdapter::sync() {
    if (inStackCall_) {
        return;  // la pile est déjà dans l'état voulu
    }
    const auto count = static_cast<int>(history_.size());
    const auto index = static_cast<int>(history_.undoCount());
    if (count == stack_->count() && index == stack_->index()) {
        // Même forme : seule la dernière entrée a pu changer (fusion de commandes continues).
        if (index > 0) {
            const_cast<QUndoCommand*>(stack_->command(index - 1))
                ->setText(commandText(history_.entry(static_cast<std::size_t>(index - 1))));
        }
        return;
    }
    syncing_ = true;
    stack_->clear();
    stack_->setUndoLimit(static_cast<int>(history_.limit()));
    for (std::size_t i = 0; i < history_.size(); ++i) {
        stack_->push(new Mirror(*this, commandText(history_.entry(i))));
    }
    stack_->setIndex(index);
    syncing_ = false;
}

}  // namespace rigger::editor
