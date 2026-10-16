// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/PoseLibraryPanel.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "editor/Document.hpp"

namespace rigger::editor {

PoseLibraryPanel::PoseLibraryPanel(Document* document, QWidget* parent)
    : QWidget(parent),
      document_(document),
      list_(new QListWidget(this)),
      nameEdit_(new QLineEdit(this)),
      selectedOnly_(new QCheckBox(this)),
      saveButton_(new QPushButton(this)),
      applyButton_(new QPushButton(this)),
      deleteButton_(new QPushButton(this)) {
    setObjectName("poseLibraryPanel");
    list_->setObjectName("poseList");
    nameEdit_->setObjectName("poseName");
    saveButton_->setObjectName("poseSave");
    applyButton_->setObjectName("poseApply");
    deleteButton_->setObjectName("poseDelete");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* saveRow = new QHBoxLayout;
    saveRow->addWidget(nameEdit_, 1);
    saveRow->addWidget(saveButton_);
    root->addLayout(saveRow);
    root->addWidget(selectedOnly_);
    root->addWidget(list_, 1);
    auto* actionRow = new QHBoxLayout;
    actionRow->addWidget(applyButton_);
    actionRow->addWidget(deleteButton_);
    root->addLayout(actionRow);

    connect(saveButton_, &QPushButton::clicked, this, &PoseLibraryPanel::saveCurrent);
    connect(nameEdit_, &QLineEdit::returnPressed, this, &PoseLibraryPanel::saveCurrent);
    connect(applyButton_, &QPushButton::clicked, this, &PoseLibraryPanel::applyCurrent);
    connect(list_, &QListWidget::itemDoubleClicked, this, &PoseLibraryPanel::applyCurrent);
    connect(deleteButton_, &QPushButton::clicked, this, &PoseLibraryPanel::deleteCurrent);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        applyButton_->setEnabled(row >= 0);
        deleteButton_->setEnabled(row >= 0);
    });
    connect(document_, &Document::animationChanged, this, &PoseLibraryPanel::refresh);
    connect(document_, &Document::characterChanged, this, &PoseLibraryPanel::refresh);
    retranslate();
    refresh();
}

void PoseLibraryPanel::refresh() {
    const int keep = list_->currentRow();
    list_->clear();
    const auto character = document_->character();
    if (character) {
        for (const NamedPose& pose : character->poses) {
            list_->addItem(QString::fromStdString(pose.name));
        }
    }
    list_->setCurrentRow(std::min(keep, list_->count() - 1));
    const bool has = document_->hasCharacter();
    nameEdit_->setEnabled(has);
    saveButton_->setEnabled(has);
    selectedOnly_->setEnabled(has);
    applyButton_->setEnabled(list_->currentRow() >= 0);
    deleteButton_->setEnabled(list_->currentRow() >= 0);
}

void PoseLibraryPanel::saveCurrent() {
    QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        name = tr("Pose %1").arg(list_->count() + 1);
    }
    const auto result = document_->savePose(name, selectedOnly_->isChecked());
    if (!result) {
        emit statusMessage(QString::fromStdString(result.error().message));
        return;
    }
    nameEdit_->clear();
    list_->setCurrentRow(list_->count() - 1);
}

void PoseLibraryPanel::applyCurrent() {
    const int row = list_->currentRow();
    if (row >= 0) {
        document_->applyPose(static_cast<std::size_t>(row));
    }
}

void PoseLibraryPanel::deleteCurrent() {
    const int row = list_->currentRow();
    if (row >= 0) {
        document_->deletePose(static_cast<std::size_t>(row));
    }
}

void PoseLibraryPanel::retranslate() {
    nameEdit_->setPlaceholderText(tr("Pose name"));
    saveButton_->setText(tr("Save Pose"));
    saveButton_->setToolTip(tr("Store the current pose in the library"));
    selectedOnly_->setText(tr("Selected bones only"));
    applyButton_->setText(tr("Apply"));
    applyButton_->setToolTip(tr("Apply the selected pose (double-click)"));
    deleteButton_->setText(tr("Delete"));
}

void PoseLibraryPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
