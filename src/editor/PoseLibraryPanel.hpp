// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace rigger::editor {

class Document;

// Panneau « Poses » : bibliothèque de poses nommées (sauvegarde de la pose courante, application par
// double-clic, suppression). Tout passe par des commandes annulables du Document.
class PoseLibraryPanel : public QWidget {
    Q_OBJECT
public:
    explicit PoseLibraryPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();
    QListWidget* list() const { return list_; }

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void refresh();
    void saveCurrent();
    void applyCurrent();
    void deleteCurrent();

    Document* document_;
    QListWidget* list_;
    QLineEdit* nameEdit_;
    QCheckBox* selectedOnly_;
    QPushButton* saveButton_;
    QPushButton* applyButton_;
    QPushButton* deleteButton_;
};

}  // namespace rigger::editor
