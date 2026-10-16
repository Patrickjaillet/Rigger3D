// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QObject>
#include <functional>

class QUndoStack;

namespace rigger {
class History;
}

namespace rigger::editor {

// Adaptateur entre l'historique du core (sans Qt, source de vérité) et un QUndoStack utilisé pour l'affichage
// (QUndoView) : chaque entrée du core est miroitée par un QUndoCommand dont le texte est le nom traduit de la
// commande. Quand l'utilisateur annule / rétablit depuis la vue (QUndoStack::setIndex), les enveloppes
// relaient l'opération au Document ; quand le core change, sync() reconstruit le miroir.
class UndoAdapter : public QObject {
    Q_OBJECT
public:
    UndoAdapter(History& history, QObject* parent = nullptr);

    QUndoStack* stack() const { return stack_; }
    void setHandlers(std::function<void()> undo, std::function<void()> redo);

public slots:
    void sync();

private:
    class Mirror;
    friend class Mirror;

    History& history_;
    QUndoStack* stack_;
    std::function<void()> undo_;
    std::function<void()> redo_;
    bool syncing_ = false;      // reconstruction du miroir : les enveloppes ne relaient rien
    bool inStackCall_ = false;  // opération lancée par la pile : le core est déjà à jour après le relais
};

}  // namespace rigger::editor
