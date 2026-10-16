// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/SkinPanel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "editor/Document.hpp"

namespace rigger::editor {

namespace {
QDoubleSpinBox* spin(QWidget* parent, const char* name, double lo, double hi, double step, int decimals) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(lo, hi);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    return box;
}
}  // namespace

SkinPanel::SkinPanel(Document* document, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName("skinPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto* modes = new QHBoxLayout;
    paintButton_ = new QPushButton(this);
    paintButton_->setObjectName("skinPaint");
    paintButton_->setCheckable(true);
    selectButton_ = new QPushButton(this);
    selectButton_->setObjectName("skinSelectVertices");
    selectButton_->setCheckable(true);
    selectShape_ = new QComboBox(this);
    selectShape_->setObjectName("skinSelectShape");
    selectShape_->addItem(QString(), 0);
    selectShape_->addItem(QString(), 1);
    modes->addWidget(paintButton_);
    modes->addWidget(selectButton_);
    modes->addWidget(selectShape_);
    root->addLayout(modes);

    brushBox_ = new QGroupBox(this);
    auto* form = new QFormLayout(brushBox_);
    mode_ = new QComboBox(this);
    mode_->setObjectName("skinMode");
    for (int i = 0; i < 4; ++i) mode_->addItem(QString(), i);
    radius_ = spin(this, "skinRadius", 0.001, 10.0, 0.01, 3);
    strength_ = spin(this, "skinStrength", 0.0, 1.0, 0.05, 2);
    falloff_ = new QComboBox(this);
    falloff_->setObjectName("skinFalloff");
    for (int i = 0; i < 3; ++i) falloff_->addItem(QString(), i);
    value_ = spin(this, "skinValue", 0.0, 1.0, 0.05, 2);
    modeLabel_ = new QLabel(this);
    radiusLabel_ = new QLabel(this);
    strengthLabel_ = new QLabel(this);
    falloffLabel_ = new QLabel(this);
    valueLabel_ = new QLabel(this);
    form->addRow(modeLabel_, mode_);
    form->addRow(radiusLabel_, radius_);
    form->addRow(strengthLabel_, strength_);
    form->addRow(falloffLabel_, falloff_);
    form->addRow(valueLabel_, value_);
    normalize_ = new QCheckBox(this);
    normalize_->setObjectName("skinNormalize");
    visibleOnly_ = new QCheckBox(this);
    visibleOnly_->setObjectName("skinVisibleOnly");
    mirror_ = new QCheckBox(this);
    mirror_->setObjectName("skinMirror");
    form->addRow(normalize_);
    form->addRow(visibleOnly_);
    form->addRow(mirror_);
    root->addWidget(brushBox_);

    auto* boneRow = new QHBoxLayout;
    boneLabel_ = new QLabel(this);
    boneLabel_->setObjectName("skinBone");
    lockBone_ = new QCheckBox(this);
    lockBone_->setObjectName("skinLockBone");
    boneRow->addWidget(boneLabel_, 1);
    boneRow->addWidget(lockBone_);
    root->addLayout(boneRow);

    vertexBox_ = new QGroupBox(this);
    auto* vertexForm = new QFormLayout(vertexBox_);
    vertexCount_ = new QLabel(this);
    exactWeight_ = spin(this, "skinExactWeight", 0.0, 1.0, 0.05, 3);
    exactWeight_->setValue(1.0);
    applyWeight_ = new QPushButton(this);
    applyWeight_->setObjectName("skinApplyWeight");
    vertexForm->addRow(vertexCount_);
    vertexForm->addRow(exactWeight_);
    vertexForm->addRow(applyWeight_);
    root->addWidget(vertexBox_);

    toolsBox_ = new QGroupBox(this);
    auto* tools = new QVBoxLayout(toolsBox_);
    autoMethod_ = new QComboBox(this);
    autoMethod_->setObjectName("skinAutoMethod");
    autoMethod_->addItem(QString(), 0);
    autoMethod_->addItem(QString(), 1);
    autoMethodLabel_ = new QLabel(this);
    auto* methodRow = new QHBoxLayout;
    methodRow->addWidget(autoMethodLabel_);
    methodRow->addWidget(autoMethod_, 1);
    tools->addLayout(methodRow);
    autoSkin_ = new QPushButton(this);
    autoSkin_->setObjectName("skinAuto");
    normalizeAll_ = new QPushButton(this);
    normalizeAll_->setObjectName("skinNormalizeAll");
    limit_ = new QPushButton(this);
    limit_->setObjectName("skinLimit");
    prune_ = new QPushButton(this);
    prune_->setObjectName("skinPrune");
    auto* copyRow = new QHBoxLayout;
    sourceMesh_ = new QComboBox(this);
    sourceMesh_->setObjectName("skinSourceMesh");
    targetMesh_ = new QComboBox(this);
    targetMesh_->setObjectName("skinTargetMesh");
    copy_ = new QPushButton(this);
    copy_->setObjectName("skinCopy");
    copyRow->addWidget(sourceMesh_);
    copyRow->addWidget(targetMesh_);
    tools->addWidget(autoSkin_);
    tools->addWidget(normalizeAll_);
    tools->addWidget(limit_);
    tools->addWidget(prune_);
    tools->addLayout(copyRow);
    tools->addWidget(copy_);
    root->addWidget(toolsBox_);
    root->addStretch(1);

    connect(paintButton_, &QPushButton::clicked, this, [this](bool on) { document_->setPaintActive(on); });
    connect(selectButton_, &QPushButton::clicked, this,
            [this](bool on) { document_->setVertexSelectMode(on); });
    connect(selectShape_, &QComboBox::activated, this, [this](int index) {
        document_->setVertexSelectShape(index == 1 ? VertexSelectShape::Lasso : VertexSelectShape::Rectangle);
    });
    connect(mode_, &QComboBox::activated, this, [this] { pushBrush(); });
    connect(falloff_, &QComboBox::activated, this, [this] { pushBrush(); });
    for (QDoubleSpinBox* box : {radius_, strength_, value_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { pushBrush(); });
    }
    connect(normalize_, &QCheckBox::clicked, document_, &Document::setAutoNormalizeWeights);
    connect(visibleOnly_, &QCheckBox::clicked, document_, &Document::setPaintOnlyVisible);
    connect(mirror_, &QCheckBox::clicked, document_, &Document::setSymmetryX);
    connect(lockBone_, &QCheckBox::clicked, this, [this](bool on) {
        if (document_->activeBone() != kNoBone) document_->setWeightLocked(document_->activeBone(), on);
    });
    connect(applyWeight_, &QPushButton::clicked, this, [this] {
        if (!document_->setSelectedVertexWeight(static_cast<float>(exactWeight_->value()))) {
            report(tr("Select a bone and some vertices first"));
        }
    });
    connect(autoSkin_, &QPushButton::clicked, this, [this] {
        AutoSkinOptions options;
        options.method = autoMethod_->currentData().toInt() == 1 ? AutoSkinMethod::HeatDiffusion
                                                                 : AutoSkinMethod::Distance;
        const auto result = document_->autoSkin(options);
        result ? reportCount(tr("Auto skin"), result.value())
               : report(QString::fromStdString(result.error().message));
    });
    connect(normalizeAll_, &QPushButton::clicked, this, [this] {
        const auto result = document_->normalizeSkinWeights();
        if (result) reportCount(tr("Normalize"), result.value());
    });
    connect(limit_, &QPushButton::clicked, this, [this] {
        const auto result = document_->limitSkinInfluences(4);
        if (result) reportCount(tr("Limit to 4 influences"), result.value());
    });
    connect(prune_, &QPushButton::clicked, this, [this] {
        const auto result = document_->pruneSkinWeights(0.01f);
        if (result) reportCount(tr("Prune"), result.value());
    });
    connect(copy_, &QPushButton::clicked, this, [this] {
        const auto result = document_->copySkinWeights(static_cast<std::size_t>(sourceMesh_->currentIndex()),
                                                       static_cast<std::size_t>(targetMesh_->currentIndex()));
        result ? reportCount(tr("Copy weights"), result.value())
               : report(QString::fromStdString(result.error().message));
    });
    for (auto signal :
         {&Document::paintSettingsChanged, &Document::vertexSelectionChanged, &Document::weightLocksChanged,
          &Document::selectionChanged, &Document::characterChanged, &Document::symmetryChanged}) {
        connect(document_, signal, this, [this] { refresh(); });
    }
    connect(document_, &Document::modeChanged, this, [this] { refresh(); });
    retranslate();
    refresh();
}

void SkinPanel::report(const QString& message) { emit statusMessage(message); }

void SkinPanel::reportCount(const QString& what, std::size_t count) {
    report(tr("%1: %2 vertices changed").arg(what).arg(count));
}

void SkinPanel::pushBrush() {
    if (updating_) return;
    Brush brush = document_->brush();
    brush.mode = static_cast<BrushMode>(mode_->currentData().toInt());
    brush.falloff = static_cast<Falloff>(falloff_->currentData().toInt());
    brush.radius = static_cast<float>(radius_->value());
    brush.strength = static_cast<float>(strength_->value());
    brush.value = static_cast<float>(value_->value());
    document_->setBrush(brush);
}

void SkinPanel::refresh() {
    updating_ = true;
    const Brush& brush = document_->brush();
    paintButton_->setChecked(document_->paintActive());
    selectButton_->setChecked(document_->vertexSelectMode());
    selectShape_->setCurrentIndex(document_->vertexSelectShape() == VertexSelectShape::Lasso ? 1 : 0);
    selectShape_->setEnabled(document_->vertexSelectMode());
    mode_->setCurrentIndex(static_cast<int>(brush.mode));
    falloff_->setCurrentIndex(static_cast<int>(brush.falloff));
    radius_->setValue(static_cast<double>(brush.radius));
    strength_->setValue(static_cast<double>(brush.strength));
    value_->setValue(static_cast<double>(brush.value));
    value_->setEnabled(brush.mode == BrushMode::Replace);
    normalize_->setChecked(document_->autoNormalizeWeights());
    visibleOnly_->setChecked(document_->paintOnlyVisible());
    mirror_->setChecked(document_->symmetryX());
    const BoneId bone = document_->activeBone();
    const bool has = document_->hasCharacter();
    boneLabel_->setText(
        bone != kNoBone
            ? tr("Active bone: %1").arg(QString::fromStdString(document_->skeleton()->bone(bone).name))
            : tr("Active bone: none (select one)"));
    lockBone_->setEnabled(bone != kNoBone);
    lockBone_->setChecked(bone != kNoBone && document_->isWeightLocked(bone));
    vertexCount_->setText(tr("Selected vertices: %1").arg(document_->vertexSelection().size()));
    applyWeight_->setEnabled(bone != kNoBone && !document_->vertexSelection().empty());
    for (QWidget* w : std::initializer_list<QWidget*>{paintButton_, selectButton_, brushBox_, toolsBox_}) {
        w->setEnabled(has);
    }
    const int meshes = has ? static_cast<int>(document_->character()->meshes.size()) : 0;
    if (sourceMesh_->count() != meshes) {
        sourceMesh_->clear();
        targetMesh_->clear();
        for (int i = 0; i < meshes; ++i) {
            sourceMesh_->addItem(tr("Mesh %1").arg(i + 1));
            targetMesh_->addItem(tr("Mesh %1").arg(i + 1));
        }
        if (meshes > 1) targetMesh_->setCurrentIndex(1);
    }
    copy_->setEnabled(meshes > 1);
    sourceMesh_->setEnabled(meshes > 1);
    targetMesh_->setEnabled(meshes > 1);
    updating_ = false;
}

void SkinPanel::retranslate() {
    paintButton_->setText(tr("Paint"));
    paintButton_->setToolTip(
        tr("Paint weights on the mesh ([ and ] change the brush size). Off: pose the bones to test the "
           "deformation."));
    selectShape_->setItemText(0, tr("Rectangle"));
    selectShape_->setItemText(1, tr("Lasso"));
    selectShape_->setToolTip(tr("Shape of the vertex selection drag"));
    autoMethodLabel_->setText(tr("Method"));
    autoMethod_->setItemText(0, tr("Distance"));
    autoMethod_->setItemText(1, tr("Heat diffusion"));
    autoMethod_->setToolTip(tr("Heat diffusion follows the mesh topology: smoother weights, slower"));
    selectButton_->setText(tr("Select Vertices"));
    selectButton_->setToolTip(
        tr("Click, drag a rectangle or draw a lasso to select vertices, then type an exact weight"));
    brushBox_->setTitle(tr("Brush"));
    modeLabel_->setText(tr("Mode"));
    const QStringList modes{tr("Add"), tr("Subtract"), tr("Replace"), tr("Smooth")};
    for (int i = 0; i < modes.size(); ++i) mode_->setItemText(i, modes[i]);
    radiusLabel_->setText(tr("Radius"));
    strengthLabel_->setText(tr("Strength"));
    falloffLabel_->setText(tr("Falloff"));
    const QStringList falloffs{tr("Smooth"), tr("Linear"), tr("Constant")};
    for (int i = 0; i < falloffs.size(); ++i) falloff_->setItemText(i, falloffs[i]);
    valueLabel_->setText(tr("Target weight"));
    normalize_->setText(tr("Normalize automatically"));
    normalize_->setToolTip(tr("Keep the weights of each vertex summing to 1 (at most 4 influences)"));
    visibleOnly_->setText(tr("Visible vertices only"));
    visibleOnly_->setToolTip(tr("Ignore vertices hidden behind other parts of the mesh"));
    mirror_->setText(tr("Mirror X"));
    lockBone_->setText(tr("Lock weights"));
    lockBone_->setToolTip(tr("Locked bones keep their weights whatever you paint"));
    vertexBox_->setTitle(tr("Vertex Weight"));
    exactWeight_->setToolTip(tr("Exact weight of the active bone"));
    applyWeight_->setText(tr("Set Weight"));
    toolsBox_->setTitle(tr("Tools"));
    autoSkin_->setText(tr("Auto Skin"));
    autoSkin_->setToolTip(
        tr("Compute weights from the distance to the bones (replaces the current weights)"));
    normalizeAll_->setText(tr("Normalize All"));
    limit_->setText(tr("Limit to 4 Influences"));
    prune_->setText(tr("Prune Below 0.01"));
    copy_->setText(tr("Copy Weights (Nearest Vertex)"));
    copy_->setToolTip(tr("Copy the weights of the first mesh onto the second, vertex by nearest vertex"));
    refresh();
}

void SkinPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
