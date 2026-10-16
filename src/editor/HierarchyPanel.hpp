// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QWidget>

class QAction;
class QLineEdit;
class QSortFilterProxyModel;
class QTreeView;

namespace rigger::editor {

class BoneTreeModel;
class Document;

// Panneau « Hiérarchie » : arbre d'os (BoneTreeModel) + filtre de recherche. La sélection est synchronisée
// avec le Document (donc avec le viewport et l'inspecteur).
class HierarchyPanel : public QWidget {
    Q_OBJECT
public:
    explicit HierarchyPanel(Document* document, QWidget* parent = nullptr);
    void retranslate();

    BoneTreeModel* model() const { return model_; }
    QSortFilterProxyModel* proxy() const { return proxy_; }
    QTreeView* tree() const { return tree_; }
    QLineEdit* filterEdit() const { return filter_; }

signals:
    void statusMessage(const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void onViewSelectionChanged();
    void onDocumentSelectionChanged();
    void onClicked(const QModelIndex& proxyIndex);

    Document* document_;
    BoneTreeModel* model_;
    QSortFilterProxyModel* proxy_;
    QTreeView* tree_;
    QLineEdit* filter_;
    QAction* searchAction_ = nullptr;
    bool syncing_ = false;
};

}  // namespace rigger::editor
