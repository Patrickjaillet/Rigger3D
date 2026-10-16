// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QAbstractItemModel>
#include <vector>

#include "core/skeleton/skeleton.hpp"

namespace rigger::editor {

class Document;

// Modèle arborescent de la hiérarchie d'os du Document (3 colonnes : nom, visibilité, verrou).
// - Renommage par EditRole ; glisser-déposer pour re-parenter (position monde conservée, cycles refusés).
// - Reconstruit sur characterChanged / hierarchyChanged ; simple dataChanged sur boneChanged.
class BoneTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { NameColumn = 0, VisibleColumn = 1, LockedColumn = 2, ColumnCount };
    enum Role { BoneIdRole = Qt::UserRole + 1 };
    static constexpr const char* kMimeType = "application/x-rigger-bones";

    explicit BoneTreeModel(Document* document, QObject* parent = nullptr);

    BoneId boneAt(const QModelIndex& index) const;
    QModelIndex indexOf(BoneId id, int column = NameColumn) const;
    void refresh();  // reconstruit (p. ex. après un changement de thème : les icônes sont recolorées)

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override { return {QString::fromLatin1(kMimeType)}; }
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

signals:
    void errorOccurred(const QString& message);

private:
    void rebuild();
    const std::vector<BoneId>& childrenOf(BoneId parent) const;

    Document* document_;
    std::vector<BoneId> roots_;
    std::vector<std::vector<BoneId>> children_;  // indexé par BoneId
    std::vector<int> rowOf_;                     // rang de l'os parmi ses frères
};

}  // namespace rigger::editor
