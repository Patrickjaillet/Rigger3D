// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/ExportDialogs.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace rigger::editor {

namespace {
QTreeWidget* makeList(QWidget* parent, const QStringList& headers) {
    auto* tree = new QTreeWidget(parent);
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(false);
    tree->setMaximumHeight(110);
    return tree;
}
}  // namespace

GltfExportDialog::GltfExportDialog(const Character& character, QWidget* parent)
    : QDialog(parent),
      meshCount_(static_cast<int>(character.meshes.size())),
      clipCount_(static_cast<int>(character.clips.size())) {
    setObjectName("gltfExportDialog");
    setWindowTitle(tr("Export glTF"));
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    format_ = new QComboBox(this);
    format_->setObjectName("exportFormat");
    format_->addItem(tr("GLB (single binary file)"), true);
    format_->addItem(tr("glTF (JSON + .bin)"), false);
    first_ = new QSpinBox(this);
    first_->setObjectName("exportFirstFrame");
    first_->setRange(0, 100000);
    last_ = new QSpinBox(this);
    last_->setObjectName("exportLastFrame");
    last_->setRange(-1, 100000);
    last_->setSpecialValueText(tr("End of clip"));
    last_->setValue(-1);
    fps_ = new QDoubleSpinBox(this);
    fps_->setObjectName("exportFps");
    fps_->setRange(0.0, 240.0);
    fps_->setDecimals(1);
    fps_->setSpecialValueText(tr("Clip FPS"));
    bakeIk_ = new QCheckBox(tr("Bake IK into the exported animations"), this);
    bakeIk_->setObjectName("exportBakeIk");
    bakeIk_->setChecked(true);
    form->addRow(tr("Format"), format_);
    form->addRow(tr("First frame"), first_);
    form->addRow(tr("Last frame"), last_);
    form->addRow(tr("Sampling rate"), fps_);
    form->addRow(bakeIk_);
    root->addLayout(form);

    root->addWidget(new QLabel(tr("Meshes"), this));
    meshes_ = makeList(this, {tr("Mesh"), tr("Vertices")});
    meshes_->setObjectName("exportMeshes");
    for (int i = 0; i < meshCount_; ++i) {
        auto* item = new QTreeWidgetItem(
            meshes_, {tr("Mesh %1").arg(i + 1),
                      QString::number(character.meshes[static_cast<std::size_t>(i)].vertices.size())});
        item->setCheckState(0, Qt::Checked);
    }
    root->addWidget(meshes_);
    root->addWidget(new QLabel(tr("Clips (double-click a name to rename)"), this));
    clips_ = makeList(this, {tr("Clip name"), tr("Duration (s)")});
    clips_->setObjectName("exportClips");
    for (int i = 0; i < clipCount_; ++i) {
        const AnimationClip& clip = character.clips[static_cast<std::size_t>(i)];
        auto* item = new QTreeWidgetItem(
            clips_, {QString::fromStdString(clip.name), QString::number(clip.duration, 'f', 2)});
        item->setCheckState(0, Qt::Checked);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    root->addWidget(clips_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

io::GltfExportOptions GltfExportDialog::options() const {
    io::GltfExportOptions o;
    o.binary = format_->currentData().toBool();
    o.firstFrame = first_->value();
    o.lastFrame = last_->value();
    o.fps = static_cast<float>(fps_->value());
    o.bakeIK = bakeIk_->isChecked();
    for (int i = 0; i < meshCount_; ++i)
        o.includeMeshes.push_back(meshes_->topLevelItem(i)->checkState(0) == Qt::Checked);
    for (int i = 0; i < clipCount_; ++i) {
        o.includeClips.push_back(clips_->topLevelItem(i)->checkState(0) == Qt::Checked);
        o.clipNames.push_back(clips_->topLevelItem(i)->text(0).toStdString());
    }
    return o;
}

ImageSequenceDialog::ImageSequenceDialog(int frameCount, bool ffmpegAvailable, QWidget* parent)
    : QDialog(parent) {
    setObjectName("imageSequenceDialog");
    setWindowTitle(tr("Export Image Sequence"));
    auto* form = new QFormLayout(this);
    first_ = new QSpinBox(this);
    first_->setObjectName("sequenceFirst");
    first_->setRange(0, std::max(frameCount - 1, 0));
    last_ = new QSpinBox(this);
    last_->setObjectName("sequenceLast");
    last_->setRange(0, std::max(frameCount - 1, 0));
    last_->setValue(std::max(frameCount - 1, 0));
    transparent_ = new QCheckBox(tr("Transparent background"), this);
    transparent_->setObjectName("sequenceTransparent");
    transparent_->setChecked(true);
    overlays_ = new QCheckBox(tr("Hide skeleton and gizmos"), this);
    overlays_->setObjectName("sequenceHideOverlays");
    overlays_->setChecked(true);
    video_ = new QCheckBox(tr("Also assemble an MP4 video (ffmpeg)"), this);
    video_->setObjectName("sequenceVideo");
    video_->setEnabled(ffmpegAvailable);
    if (!ffmpegAvailable) video_->setToolTip(tr("ffmpeg was not found in the PATH"));
    form->addRow(tr("First frame"), first_);
    form->addRow(tr("Last frame"), last_);
    form->addRow(transparent_);
    form->addRow(overlays_);
    form->addRow(video_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

ImageSequenceOptions ImageSequenceDialog::options() const {
    ImageSequenceOptions o;
    o.firstFrame = first_->value();
    o.lastFrame = std::max(last_->value(), first_->value());
    o.transparent = transparent_->isChecked();
    o.hideOverlays = overlays_->isChecked();
    o.makeVideo = video_->isChecked() && video_->isEnabled();
    return o;
}

}  // namespace rigger::editor
