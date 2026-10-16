// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/BoneTreeModel.hpp"

#include <QBrush>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <algorithm>

#include "editor/Document.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

BoneTreeModel::BoneTreeModel(Document* document, QObject* parent)
    : QAbstractItemModel(parent), document_(document) {
    rebuild();
    connect(document_, &Document::characterChanged, this, [this] {
        beginResetModel();
        rebuild();
        endResetModel();
    });
    connect(document_, &Document::hierarchyChanged, this, [this] {
        beginResetModel();
        rebuild();
        endResetModel();
    });
    connect(document_, &Document::boneChanged, this, [this](BoneId id) {
        const QModelIndex first = indexOf(id, 0);
        if (first.isValid()) {
            emit dataChanged(first, indexOf(id, ColumnCount - 1));
        }
    });
}

void BoneTreeModel::rebuild() {
    roots_.clear();
    children_.clear();
    rowOf_.clear();
    const Skeleton* skeleton = document_->skeleton();
    if (skeleton == nullptr) {
        return;
    }
    children_.resize(skeleton->slotCount());
    rowOf_.assign(skeleton->slotCount(), 0);
    for (const BoneId id : skeleton->order()) {  // parents d'abord
        const BoneId parent = skeleton->bone(id).parent;
        std::vector<BoneId>& siblings = parent == kNoBone ? roots_ : children_[parent];
        rowOf_[id] = static_cast<int>(siblings.size());
        siblings.push_back(id);
    }
}

void BoneTreeModel::refresh() {
    beginResetModel();
    rebuild();
    endResetModel();
}

const std::vector<BoneId>& BoneTreeModel::childrenOf(BoneId parent) const {
    return parent == kNoBone ? roots_ : children_[parent];
}

BoneId BoneTreeModel::boneAt(const QModelIndex& index) const {
    return index.isValid() ? static_cast<BoneId>(index.internalId()) : kNoBone;
}

QModelIndex BoneTreeModel::indexOf(BoneId id, int column) const {
    const Skeleton* skeleton = document_->skeleton();
    if (skeleton == nullptr || !skeleton->contains(id) || id >= rowOf_.size()) {
        return {};
    }
    return createIndex(rowOf_[id], column, static_cast<quintptr>(id));
}

QModelIndex BoneTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (row < 0 || column < 0 || column >= ColumnCount || !hasIndex(row, column, parent)) {
        return {};
    }
    const std::vector<BoneId>& siblings = childrenOf(boneAt(parent));
    return createIndex(row, column, static_cast<quintptr>(siblings[static_cast<std::size_t>(row)]));
}

QModelIndex BoneTreeModel::parent(const QModelIndex& child) const {
    const Skeleton* skeleton = document_->skeleton();
    if (!child.isValid() || skeleton == nullptr) {
        return {};
    }
    const BoneId parentId = skeleton->bone(boneAt(child)).parent;
    return parentId == kNoBone ? QModelIndex() : indexOf(parentId, 0);
}

int BoneTreeModel::rowCount(const QModelIndex& parent) const {
    if (document_->skeleton() == nullptr || parent.column() > 0) {
        return 0;
    }
    return static_cast<int>(childrenOf(boneAt(parent)).size());
}

int BoneTreeModel::columnCount(const QModelIndex&) const { return ColumnCount; }

QVariant BoneTreeModel::data(const QModelIndex& index, int role) const {
    const Skeleton* skeleton = document_->skeleton();
    if (!index.isValid() || skeleton == nullptr) {
        return {};
    }
    const Bone& bone = skeleton->bone(boneAt(index));
    const bool hidden = hasFlag(bone.flags, BoneFlags::Hidden);
    const bool locked = hasFlag(bone.flags, BoneFlags::Locked);
    if (role == BoneIdRole) {
        return bone.id;
    }
    switch (index.column()) {
        case NameColumn:
            switch (role) {
                case Qt::DisplayRole:
                case Qt::EditRole:
                    return QString::fromStdString(bone.name);
                case Qt::DecorationRole:
                    return Icons::get(QStringLiteral("bone"));
                case Qt::ForegroundRole:
                    return hidden ? QVariant(QBrush(Theme::colors().textDim)) : QVariant();
                case Qt::ToolTipRole:
                    return tr("%1 (bone %2)").arg(QString::fromStdString(bone.name)).arg(bone.id);
                default:
                    return {};
            }
        case VisibleColumn:
            if (role == Qt::DecorationRole) {
                return Icons::get(hidden ? QStringLiteral("eye-off") : QStringLiteral("eye"));
            }
            if (role == Qt::ToolTipRole) {
                return hidden ? tr("Hidden - click to show") : tr("Visible - click to hide");
            }
            return {};
        case LockedColumn:
            if (role == Qt::DecorationRole) {
                return Icons::get(locked ? QStringLiteral("lock") : QStringLiteral("unlock"));
            }
            if (role == Qt::ToolTipRole) {
                return locked ? tr("Locked - click to unlock") : tr("Unlocked - click to lock");
            }
            return {};
        default:
            return {};
    }
}

bool BoneTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.column() != NameColumn || role != Qt::EditRole) {
        return false;
    }
    const auto result = document_->renameBone(boneAt(index), value.toString());
    if (!result) {
        emit errorOccurred(QString::fromStdString(result.error().message));
        return false;
    }
    return true;  // dataChanged est émis via Document::boneChanged
}

Qt::ItemFlags BoneTreeModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;  // dépôt à la racine
    }
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == NameColumn) {
        f |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    }
    return f;
}

QVariant BoneTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) {
        return {};
    }
    if (role == Qt::DisplayRole && section == NameColumn) {
        return tr("Bone");
    }
    if (role == Qt::DecorationRole && section == VisibleColumn) {
        return Icons::get(QStringLiteral("eye"));
    }
    if (role == Qt::DecorationRole && section == LockedColumn) {
        return Icons::get(QStringLiteral("lock"));
    }
    if (role == Qt::ToolTipRole && section == VisibleColumn) {
        return tr("Visibility");
    }
    if (role == Qt::ToolTipRole && section == LockedColumn) {
        return tr("Lock");
    }
    return {};
}

QMimeData* BoneTreeModel::mimeData(const QModelIndexList& indexes) const {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    std::vector<BoneId> ids;
    for (const QModelIndex& index : indexes) {
        if (index.column() == NameColumn && std::find(ids.begin(), ids.end(), boneAt(index)) == ids.end()) {
            ids.push_back(boneAt(index));
        }
    }
    stream << static_cast<quint32>(ids.size());
    for (const BoneId id : ids) {
        stream << static_cast<quint32>(id);
    }
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMimeType), bytes);
    return mime;
}

namespace {
std::vector<BoneId> decode(const QMimeData* data) {
    std::vector<BoneId> ids;
    QByteArray bytes = data->data(QString::fromLatin1(BoneTreeModel::kMimeType));
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    quint32 count = 0;
    stream >> count;
    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        quint32 id = 0;
        stream >> id;
        ids.push_back(static_cast<BoneId>(id));
    }
    return ids;
}
}  // namespace

bool BoneTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int, int,
                                    const QModelIndex& parent) const {
    const Skeleton* skeleton = document_->skeleton();
    if (skeleton == nullptr || action != Qt::MoveAction || !data->hasFormat(QString::fromLatin1(kMimeType))) {
        return false;
    }
    const BoneId target = boneAt(parent);
    for (const BoneId id : decode(data)) {
        // Refuse de déposer un os sur lui-même ou sur un de ses descendants (cycle).
        if (!skeleton->contains(id) || id == target ||
            (target != kNoBone && skeleton->isDescendantOf(target, id))) {
            return false;
        }
    }
    return true;
}

bool BoneTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                 const QModelIndex& parent) {
    if (!canDropMimeData(data, action, row, column, parent)) {
        return false;
    }
    const BoneId target = boneAt(parent);
    bool any = false;
    for (const BoneId id : decode(data)) {
        const auto result = document_->reparentBone(id, target, true);
        if (!result) {
            emit errorOccurred(QString::fromStdString(result.error().message));
            return false;
        }
        any = true;
    }
    return any;
}

}  // namespace rigger::editor
