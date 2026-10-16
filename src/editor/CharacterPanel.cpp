// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/CharacterPanel.hpp"

#include <QComboBox>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "editor/Document.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

CharacterPanel::CharacterPanel(Document* document, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName("characterPanel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    summary_ = new QLabel(this);
    summary_->setObjectName("characterSummary");
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    skinLabel_ = new QLabel(this);
    layout->addWidget(skinLabel_);
    skinCombo_ = new QComboBox(this);
    skinCombo_->setObjectName("characterSkinMethod");
    layout->addWidget(skinCombo_);

    clipLabel_ = new QLabel(this);
    layout->addWidget(clipLabel_);
    clipCombo_ = new QComboBox(this);
    clipCombo_->setObjectName("characterClip");
    layout->addWidget(clipCombo_);

    meshesLabel_ = new QLabel(this);
    layout->addWidget(meshesLabel_);
    meshTree_ = new QTreeWidget(this);
    meshTree_->setObjectName("characterMeshTree");
    meshTree_->setColumnCount(2);
    meshTree_->setRootIsDecorated(true);
    meshTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    meshTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(meshTree_, 1);

    connect(skinCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!updating_ && index >= 0) {
            document_->setSkinMethod(index == 1 ? SkinMethod::Dqs : SkinMethod::Lbs);
        }
    });
    connect(clipCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!updating_) {
            document_->setClipIndex(index);
        }
    });
    connect(document_, &Document::characterChanged, this, &CharacterPanel::rebuild);
    connect(document_, &Document::skinMethodChanged, this, &CharacterPanel::syncControls);
    connect(document_, &Document::clipChanged, this, &CharacterPanel::syncControls);
    retranslate();
}

void CharacterPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void CharacterPanel::retranslate() {
    skinLabel_->setText(tr("Skinning method"));
    clipLabel_->setText(tr("Animation"));
    meshesLabel_->setText(tr("Meshes and materials"));
    meshTree_->setHeaderLabels({tr("Name"), tr("Info")});
    updating_ = true;
    skinCombo_->clear();
    skinCombo_->addItem(tr("Linear Blend Skinning (LBS)"));
    skinCombo_->addItem(tr("Dual Quaternion Skinning (DQS)"));
    updating_ = false;
    rebuild();
}

void CharacterPanel::syncControls() {
    updating_ = true;
    skinCombo_->setCurrentIndex(document_->skinMethod() == SkinMethod::Dqs ? 1 : 0);
    clipCombo_->setCurrentIndex(document_->clipIndex());
    updating_ = false;
}

void CharacterPanel::rebuild() {
    updating_ = true;
    meshTree_->clear();
    clipCombo_->clear();
    const auto character = document_->character();
    if (!character) {
        summary_->setText(tr("No character loaded."));
        skinCombo_->setEnabled(false);
        clipCombo_->setEnabled(false);
        updating_ = false;
        return;
    }
    skinCombo_->setEnabled(true);
    summary_->setText(tr("<b>%1</b><br>%2 bones, %3 meshes, %4 vertices, %5 animations")
                          .arg(QString::fromStdString(character->name))
                          .arg(character->skeleton.boneCount())
                          .arg(character->meshes.size())
                          .arg(document_->vertexCount())
                          .arg(character->clips.size()));

    for (const AnimationClip& clip : character->clips) {
        clipCombo_->addItem(
            tr("%1 (%2 s)").arg(QString::fromStdString(clip.name)).arg(clip.duration, 0, 'f', 2));
    }
    clipCombo_->setEnabled(!character->clips.empty());

    int meshNumber = 0;
    for (const Mesh& mesh : character->meshes) {
        auto* item = new QTreeWidgetItem(meshTree_);
        const QString name = !mesh.submeshes.empty() && !mesh.submeshes.front().name.empty()
                                 ? QString::fromStdString(mesh.submeshes.front().name).section('.', 0, 0)
                                 : tr("Mesh %1").arg(meshNumber);
        item->setText(0, name);
        item->setIcon(0, Icons::get(QStringLiteral("mesh")));
        item->setText(1, tr("%1 vertices, %2 triangles%3")
                             .arg(mesh.vertices.size())
                             .arg(mesh.indices.size() / 3)
                             .arg(mesh.hasSkinning() ? tr(", skinned") : QString()));
        for (const Submesh& submesh : mesh.submeshes) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(0, QString::fromStdString(submesh.material.name));
            QPixmap swatch(14, 14);
            const glm::vec3 c = submesh.material.baseColor;
            swatch.fill(QColor::fromRgbF(static_cast<double>(c.r), static_cast<double>(c.g),
                                         static_cast<double>(c.b)));
            child->setIcon(0, QIcon(swatch));
            child->setText(1, tr("metallic %1, roughness %2%3")
                                  .arg(static_cast<double>(submesh.material.metallic), 0, 'f', 2)
                                  .arg(static_cast<double>(submesh.material.roughness), 0, 'f', 2)
                                  .arg(submesh.material.baseColorTexture ? tr(", textured") : QString()));
        }
        item->setExpanded(true);
        ++meshNumber;
    }
    updating_ = false;
    syncControls();
}

}  // namespace rigger::editor
