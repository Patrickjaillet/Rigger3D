// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/HierarchyPanel.hpp"

#include <QEvent>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVBoxLayout>
#include <vector>

#include "editor/BoneTreeModel.hpp"
#include "editor/Document.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

HierarchyPanel::HierarchyPanel(Document* document, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName("hierarchyPanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    filter_ = new QLineEdit(this);
    filter_->setObjectName("hierarchyFilter");
    filter_->setClearButtonEnabled(true);
    searchAction_ = filter_->addAction(Icons::get(QStringLiteral("search")), QLineEdit::LeadingPosition);
    layout->addWidget(filter_);

    model_ = new BoneTreeModel(document_, this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterKeyColumn(BoneTreeModel::NameColumn);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setRecursiveFilteringEnabled(true);  // garde les parents des os qui correspondent

    tree_ = new QTreeView(this);
    tree_->setObjectName("hierarchyTree");
    tree_->setModel(proxy_);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);  // Ctrl : ajout, Maj : plage
    tree_->setDragDropMode(QAbstractItemView::DragDrop);
    tree_->setDefaultDropAction(Qt::MoveAction);
    tree_->setDropIndicatorShown(true);
    tree_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    tree_->setUniformRowHeights(true);
    tree_->setAllColumnsShowFocus(true);
    tree_->setIconSize(QSize(16, 16));
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(BoneTreeModel::NameColumn, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(BoneTreeModel::VisibleColumn, QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(BoneTreeModel::LockedColumn, QHeaderView::Fixed);
    tree_->header()->resizeSection(BoneTreeModel::VisibleColumn, 30);
    tree_->header()->resizeSection(BoneTreeModel::LockedColumn, 30);
    layout->addWidget(tree_, 1);

    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& text) {
        proxy_->setFilterFixedString(text);
        if (!text.isEmpty()) {
            tree_->expandAll();
        }
        onDocumentSelectionChanged();
    });
    connect(tree_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &HierarchyPanel::onViewSelectionChanged);
    connect(document_, &Document::selectionChanged, this, &HierarchyPanel::onDocumentSelectionChanged);
    connect(tree_, &QTreeView::clicked, this, &HierarchyPanel::onClicked);
    connect(model_, &BoneTreeModel::errorOccurred, this,
            [this](const QString& message) { emit statusMessage(tr("Hierarchy: %1").arg(message)); });
    const auto expandAndSync = [this] {
        tree_->expandAll();
        onDocumentSelectionChanged();
    };
    connect(model_, &QAbstractItemModel::modelReset, this, expandAndSync);
    retranslate();
    tree_->expandAll();
}

void HierarchyPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void HierarchyPanel::retranslate() {
    filter_->setPlaceholderText(tr("Search bones..."));
    searchAction_->setIcon(Icons::get(QStringLiteral("search")));
    model_->refresh();  // icônes recolorées selon le thème
}

void HierarchyPanel::onViewSelectionChanged() {
    if (syncing_) {
        return;
    }
    syncing_ = true;
    std::vector<BoneId> ids;
    for (const QModelIndex& index : tree_->selectionModel()->selectedRows(BoneTreeModel::NameColumn)) {
        ids.push_back(model_->boneAt(proxy_->mapToSource(index)));
    }
    document_->setSelection(std::move(ids));
    syncing_ = false;
}

void HierarchyPanel::onDocumentSelectionChanged() {
    if (syncing_) {
        return;
    }
    syncing_ = true;
    QItemSelection selection;
    QModelIndex current;
    for (const BoneId id : document_->selection()) {
        const QModelIndex proxyIndex = proxy_->mapFromSource(model_->indexOf(id, BoneTreeModel::NameColumn));
        if (proxyIndex.isValid()) {
            selection.select(proxyIndex, proxy_->index(proxyIndex.row(), BoneTreeModel::ColumnCount - 1,
                                                       proxyIndex.parent()));
            current = proxyIndex;
        }
    }
    tree_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (current.isValid()) {
        tree_->scrollTo(current);
    }
    syncing_ = false;
}

void HierarchyPanel::onClicked(const QModelIndex& proxyIndex) {
    const QModelIndex index = proxy_->mapToSource(proxyIndex);
    const BoneId id = model_->boneAt(index);
    const Skeleton* skeleton = document_->skeleton();
    if (skeleton == nullptr || id == kNoBone) {
        return;
    }
    const BoneFlags flags = skeleton->bone(id).flags;
    const auto toggled = [&](BoneFlags bit) {
        const auto value = static_cast<std::uint32_t>(flags);
        return static_cast<BoneFlags>(value ^ static_cast<std::uint32_t>(bit));
    };
    if (index.column() == BoneTreeModel::VisibleColumn) {
        document_->setBoneFlags(id, toggled(BoneFlags::Hidden));
    } else if (index.column() == BoneTreeModel::LockedColumn) {
        document_->setBoneFlags(id, toggled(BoneFlags::Locked));
    }
}

}  // namespace rigger::editor
