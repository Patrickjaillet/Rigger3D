// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QComboBox;
class QLabel;
class QTreeWidget;

namespace rigger::editor {

class Document;

// Propriétés du personnage : informations, méthode de skinning (LBS / DQS), animation courante,
// maillages et matériaux (lecture seule pour l'instant ; l'édition annulable arrive avec la Phase 8).
class CharacterPanel : public QWidget {
    Q_OBJECT
public:
    explicit CharacterPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();

    QTreeWidget* meshTree() const { return meshTree_; }
    QComboBox* skinMethodCombo() const { return skinCombo_; }
    QComboBox* clipCombo() const { return clipCombo_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void rebuild();
    void syncControls();

    Document* document_;
    QLabel* summary_ = nullptr;
    QLabel* skinLabel_ = nullptr;
    QComboBox* skinCombo_ = nullptr;
    QLabel* clipLabel_ = nullptr;
    QComboBox* clipCombo_ = nullptr;
    QLabel* meshesLabel_ = nullptr;
    QTreeWidget* meshTree_ = nullptr;
    bool updating_ = false;
};

}  // namespace rigger::editor
