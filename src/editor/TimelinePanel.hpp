// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QToolButton;

namespace rigger::editor {

class ActionRegistry;
class Document;
class TimelineView;

// Panneau « Timeline » : barre de transport (début / précédente / lecture / suivante / fin, image courante),
// clé auto, pelure d'oignon, gestion des clips (liste, nouveau, dupliquer, supprimer, fps, boucle) et vue
// timeline.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    TimelinePanel(Document* document, ActionRegistry* actions, QWidget* parent = nullptr);
    void retranslate();
    TimelineView* view() const { return view_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void refreshClips();
    void refreshValues();

    Document* document_;
    TimelineView* view_;
    QSpinBox* frameBox_;
    QComboBox* clipBox_;
    QDoubleSpinBox* fpsBox_;
    QToolButton* newClip_;
    QToolButton* duplicateClip_;
    QToolButton* deleteClip_;
    QToolButton* renameClip_;
    QToolButton* loopButton_;
    QToolButton* pingPongButton_;
    QComboBox* speedBox_;
    QLabel* frameLabel_;
    QLabel* clipLabel_;
    QLabel* fpsLabel_;
    QLabel* speedLabel_;
    bool updating_ = false;
};

}  // namespace rigger::editor
