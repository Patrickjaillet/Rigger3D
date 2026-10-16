// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/MixPanel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>

#include "editor/Document.hpp"
#include "io/gltf_import.hpp"

namespace rigger::editor {

namespace {

QDoubleSpinBox* dspin(QWidget* parent, const char* name, double lo, double hi, double step, int decimals) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(lo, hi);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    return box;
}

QPushButton* button(QWidget* parent, const char* name) {
    auto* b = new QPushButton(parent);
    b->setObjectName(name);
    return b;
}

QComboBox* combo(QWidget* parent, const char* name) {
    auto* c = new QComboBox(parent);
    c->setObjectName(name);
    return c;
}

QSlider* slider(QWidget* parent, const char* name, int hi) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setObjectName(name);
    s->setRange(0, hi);
    return s;
}

}  // namespace

MixPanel::MixPanel(Document* document, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName("mixPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* modeRow = new QHBoxLayout;
    modeLabel_ = new QLabel(this);
    mode_ = combo(this, "mixMode");
    for (int i = 0; i < 4; ++i) mode_->addItem(QString(), i);
    modeRow->addWidget(modeLabel_);
    modeRow->addWidget(mode_, 1);
    root->addLayout(modeRow);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("mixTabs");
    for (int i = 0; i < 4; ++i) {
        auto* page = new QWidget(tabs_);
        tabs_->addTab(page, QString());
        if (i == 0) buildLayersTab(page);
        if (i == 1) buildNlaTab(page);
        if (i == 2) buildTreeTab(page);
        if (i == 3) buildRetargetTab(page);
    }
    root->addWidget(tabs_, 1);

    connect(mode_, &QComboBox::activated, this, [this](int index) {
        const auto mode = static_cast<MixMode>(mode_->itemData(index).toInt());
        document_->editMix([mode](AnimationMix& m) { m.mode = mode; }, "Edit Mix");
    });
    for (auto signal : {&Document::mixChanged, &Document::animationChanged, &Document::characterChanged,
                        &Document::selectionChanged}) {
        connect(document_, signal, this, [this] { refresh(); });
    }
    retranslate();
    refresh();
}

// --- Construction des onglets
// ---------------------------------------------------------------------------------

void MixPanel::buildLayersTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    layerList_ = new QListWidget(page);
    layerList_->setObjectName("mixLayerList");
    layerList_->setMaximumHeight(100);
    v->addWidget(layerList_);
    auto* row = new QHBoxLayout;
    addLayer_ = button(page, "mixAddLayer");
    removeLayer_ = button(page, "mixRemoveLayer");
    layerUp_ = button(page, "mixLayerUp");
    layerDown_ = button(page, "mixLayerDown");
    for (QPushButton* b : {addLayer_, removeLayer_, layerUp_, layerDown_}) row->addWidget(b);
    v->addLayout(row);
    auto* form = new QFormLayout;
    layerClip_ = combo(page, "mixLayerClip");
    layerWeight_ = slider(page, "mixLayerWeight", 100);
    layerMode_ = combo(page, "mixLayerMode");
    layerMode_->addItem(QString(), 0);
    layerMode_->addItem(QString(), 1);
    layerSpeed_ = dspin(page, "mixLayerSpeed", -4.0, 4.0, 0.1, 2);
    layerOffset_ = dspin(page, "mixLayerOffset", -1000.0, 1000.0, 0.1, 2);
    layerEnabled_ = new QCheckBox(page);
    layerEnabled_->setObjectName("mixLayerEnabled");
    layerClipLabel_ = new QLabel(page);
    layerWeightLabel_ = new QLabel(page);
    layerModeLabel_ = new QLabel(page);
    layerSpeedLabel_ = new QLabel(page);
    layerOffsetLabel_ = new QLabel(page);
    form->addRow(layerClipLabel_, layerClip_);
    form->addRow(layerWeightLabel_, layerWeight_);
    form->addRow(layerModeLabel_, layerMode_);
    form->addRow(layerSpeedLabel_, layerSpeed_);
    form->addRow(layerOffsetLabel_, layerOffset_);
    form->addRow(layerEnabled_);
    layerMaskLabel_ = new QLabel(page);
    maskFromSelection_ = button(page, "mixMaskFromSelection");
    clearMask_ = button(page, "mixClearMask");
    form->addRow(layerMaskLabel_);
    form->addRow(maskFromSelection_);
    form->addRow(clearMask_);
    v->addLayout(form);
    v->addStretch(1);

    connect(layerList_, &QListWidget::currentRowChanged, this, [this] {
        if (!updating_) refreshLayers();
    });
    connect(addLayer_, &QPushButton::clicked, this, [this] {
        const int clipIndex = std::max(document_->clipIndex(), 0);
        document_->editMix(
            [&](AnimationMix& m) {
                AnimationLayer layer;
                layer.name = QStringLiteral("Layer %1").arg(m.layers.size() + 1).toStdString();
                layer.clip = clipIndex;
                m.layers.push_back(layer);
                if (m.mode == MixMode::Clip) m.mode = MixMode::Layers;
            },
            "Edit Mix");
        layerList_->setCurrentRow(layerList_->count() - 1);
    });
    connect(removeLayer_, &QPushButton::clicked, this, [this] {
        const int i = layerIndex();
        if (i >= 0)
            document_->editMix([i](AnimationMix& m) { m.layers.erase(m.layers.begin() + i); }, "Edit Mix");
    });
    const auto move = [this](int delta) {
        const int i = layerIndex();
        if (i < 0) return;
        const int j = i + delta;
        if (document_->editMix(
                [i, j](AnimationMix& m) {
                    if (j >= 0 && j < static_cast<int>(m.layers.size()))
                        std::swap(m.layers[static_cast<std::size_t>(i)],
                                  m.layers[static_cast<std::size_t>(j)]);
                },
                "Edit Mix")) {
            layerList_->setCurrentRow(j);
        }
    };
    connect(layerUp_, &QPushButton::clicked, this, [move] { move(-1); });
    connect(layerDown_, &QPushButton::clicked, this, [move] { move(1); });
    const auto edit = [this](const std::function<void(AnimationLayer&)>& f, const char* key = "") {
        const int i = layerIndex();
        if (i < 0 || updating_) return;
        document_->editMix([&](AnimationMix& m) { f(m.layers[static_cast<std::size_t>(i)]); }, "Edit Mix",
                           key[0] != 0 ? std::string(key) + std::to_string(i) : std::string());
    };
    connect(layerClip_, &QComboBox::activated, this,
            [this, edit](int index) { edit([index](AnimationLayer& l) { l.clip = index; }); });
    connect(layerWeight_, &QSlider::valueChanged, this, [edit](int value) {
        edit([value](AnimationLayer& l) { l.weight = static_cast<float>(value) / 100.0f; }, "layerWeight");
    });
    connect(layerWeight_, &QSlider::sliderReleased, document_, &Document::endContinuousEdit);
    connect(layerMode_, &QComboBox::activated, this, [edit](int index) {
        edit([index](AnimationLayer& l) { l.mode = index == 1 ? LayerMode::Additive : LayerMode::Override; });
    });
    connect(layerSpeed_, &QDoubleSpinBox::valueChanged, this, [edit](double v) {
        edit([v](AnimationLayer& l) { l.speed = static_cast<float>(v); }, "layerSpeed");
    });
    connect(layerOffset_, &QDoubleSpinBox::valueChanged, this, [edit](double v) {
        edit([v](AnimationLayer& l) { l.timeOffset = static_cast<float>(v); }, "layerOffset");
    });
    connect(layerSpeed_, &QDoubleSpinBox::editingFinished, document_, &Document::endContinuousEdit);
    connect(layerOffset_, &QDoubleSpinBox::editingFinished, document_, &Document::endContinuousEdit);
    connect(layerEnabled_, &QCheckBox::clicked, this,
            [edit](bool on) { edit([on](AnimationLayer& l) { l.enabled = on; }); });
    connect(maskFromSelection_, &QPushButton::clicked, this, [this, edit] {
        const std::vector<BoneId> selection = document_->selection();
        edit([selection](AnimationLayer& l) { l.mask = selection; });
    });
    connect(clearMask_, &QPushButton::clicked, this,
            [edit] { edit([](AnimationLayer& l) { l.mask.clear(); }); });
}

void MixPanel::buildNlaTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    stripList_ = new QListWidget(page);
    stripList_->setObjectName("mixStripList");
    stripList_->setMaximumHeight(100);
    v->addWidget(stripList_);
    auto* row = new QHBoxLayout;
    addStrip_ = button(page, "mixAddStrip");
    removeStrip_ = button(page, "mixRemoveStrip");
    row->addWidget(addStrip_);
    row->addWidget(removeStrip_);
    v->addLayout(row);
    auto* form = new QFormLayout;
    stripClip_ = combo(page, "mixStripClip");
    stripStart_ = dspin(page, "mixStripStart", 0.0, 100000.0, 0.1, 2);
    stripTrack_ = dspin(page, "mixStripTrack", 0.0, 32.0, 1.0, 0);
    stripRepeat_ = dspin(page, "mixStripRepeat", 0.0, 1000.0, 0.5, 2);
    stripSpeed_ = dspin(page, "mixStripSpeed", 0.05, 10.0, 0.1, 2);
    stripBlendIn_ = dspin(page, "mixStripBlendIn", 0.0, 1000.0, 0.1, 2);
    stripBlendOut_ = dspin(page, "mixStripBlendOut", 0.0, 1000.0, 0.1, 2);
    stripWeight_ = slider(page, "mixStripWeight", 100);
    stripMode_ = combo(page, "mixStripMode");
    stripMode_->addItem(QString(), 0);
    stripMode_->addItem(QString(), 1);
    QWidget* editors[9] = {stripClip_,    stripStart_,    stripTrack_,  stripRepeat_, stripSpeed_,
                           stripBlendIn_, stripBlendOut_, stripWeight_, stripMode_};
    for (int i = 0; i < 9; ++i) {
        stripLabels_[i] = new QLabel(page);
        form->addRow(stripLabels_[i], editors[i]);
    }
    v->addLayout(form);
    auto* fade = new QHBoxLayout;
    fadeLabel_ = new QLabel(page);
    fadeA_ = combo(page, "mixFadeA");
    fadeB_ = combo(page, "mixFadeB");
    fadeDuration_ = dspin(page, "mixFadeDuration", 0.0, 100.0, 0.1, 2);
    fadeDuration_->setValue(0.5);
    fadeButton_ = button(page, "mixCrossfade");
    fade->addWidget(fadeLabel_);
    fade->addWidget(fadeA_);
    fade->addWidget(fadeB_);
    fade->addWidget(fadeDuration_);
    v->addLayout(fade);
    v->addWidget(fadeButton_);
    v->addStretch(1);

    connect(stripList_, &QListWidget::currentRowChanged, this, [this] {
        if (!updating_) refreshStrips();
    });
    connect(addStrip_, &QPushButton::clicked, this, [this] {
        const int clipIndex = std::max(document_->clipIndex(), 0);
        document_->editMix(
            [&](AnimationMix& m) {
                NlaStrip strip;
                const auto& clips = document_->character()->clips;
                strip.name = static_cast<std::size_t>(clipIndex) < clips.size()
                                 ? clips[static_cast<std::size_t>(clipIndex)].name
                                 : "Strip";
                strip.clip = clipIndex;
                double end = 0.0;  // à la suite des bandes existantes
                for (const NlaStrip& s : m.strips) end = std::max(end, s.start + stripLength(s, clips));
                strip.start = end;
                m.strips.push_back(strip);
                if (m.mode == MixMode::Clip) m.mode = MixMode::Nla;
            },
            "Edit Mix");
        stripList_->setCurrentRow(stripList_->count() - 1);
    });
    connect(removeStrip_, &QPushButton::clicked, this, [this] {
        const int i = stripIndex();
        if (i >= 0)
            document_->editMix([i](AnimationMix& m) { m.strips.erase(m.strips.begin() + i); }, "Edit Mix");
    });
    const auto edit = [this](const std::function<void(NlaStrip&)>& f, const char* key = "") {
        const int i = stripIndex();
        if (i < 0 || updating_) return;
        document_->editMix([&](AnimationMix& m) { f(m.strips[static_cast<std::size_t>(i)]); }, "Edit Mix",
                           key[0] != 0 ? std::string(key) + std::to_string(i) : std::string());
    };
    connect(stripClip_, &QComboBox::activated, this,
            [edit](int index) { edit([index](NlaStrip& s) { s.clip = index; }); });
    const auto bindSpin = [this, edit](QDoubleSpinBox* box, const char* key,
                                       std::function<void(NlaStrip&, double)> set) {
        connect(box, &QDoubleSpinBox::valueChanged, this,
                [edit, key, set](double v) { edit([v, set](NlaStrip& s) { set(s, v); }, key); });
        connect(box, &QDoubleSpinBox::editingFinished, document_, &Document::endContinuousEdit);
    };
    bindSpin(stripStart_, "stripStart", [](NlaStrip& s, double v) { s.start = v; });
    bindSpin(stripTrack_, "stripTrack", [](NlaStrip& s, double v) { s.track = static_cast<int>(v); });
    bindSpin(stripRepeat_, "stripRepeat", [](NlaStrip& s, double v) { s.repeat = static_cast<float>(v); });
    bindSpin(stripSpeed_, "stripSpeed", [](NlaStrip& s, double v) { s.speed = static_cast<float>(v); });
    bindSpin(stripBlendIn_, "stripIn", [](NlaStrip& s, double v) { s.blendIn = v; });
    bindSpin(stripBlendOut_, "stripOut", [](NlaStrip& s, double v) { s.blendOut = v; });
    connect(stripWeight_, &QSlider::valueChanged, this, [edit](int v) {
        edit([v](NlaStrip& s) { s.weight = static_cast<float>(v) / 100.0f; }, "stripWeight");
    });
    connect(stripWeight_, &QSlider::sliderReleased, document_, &Document::endContinuousEdit);
    connect(stripMode_, &QComboBox::activated, this, [edit](int index) {
        edit([index](NlaStrip& s) { s.mode = index == 1 ? LayerMode::Additive : LayerMode::Override; });
    });
    connect(fadeButton_, &QPushButton::clicked, this, [this] {
        if (!document_->addCrossfade(fadeA_->currentIndex(), fadeB_->currentIndex(),
                                     fadeDuration_->value())) {
            emit statusMessage(tr("Choose two clips for the crossfade"));
        }
    });
}

void MixPanel::buildTreeTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    treeList_ = new QListWidget(page);
    treeList_->setObjectName("mixTreeList");
    treeList_->setMaximumHeight(100);
    v->addWidget(treeList_);
    auto* row = new QHBoxLayout;
    addEntry_ = button(page, "mixAddEntry");
    removeEntry_ = button(page, "mixRemoveEntry");
    row->addWidget(addEntry_);
    row->addWidget(removeEntry_);
    v->addLayout(row);
    auto* form = new QFormLayout;
    entryClip_ = combo(page, "mixEntryClip");
    entryThreshold_ = dspin(page, "mixEntryThreshold", -1000.0, 1000.0, 0.5, 2);
    entryClipLabel_ = new QLabel(page);
    entryThresholdLabel_ = new QLabel(page);
    form->addRow(entryClipLabel_, entryClip_);
    form->addRow(entryThresholdLabel_, entryThreshold_);
    treeParameterLabel_ = new QLabel(page);
    treeParameter_ = slider(page, "mixTreeParameter", 1000);
    treeSync_ = new QCheckBox(page);
    treeSync_->setObjectName("mixTreeSync");
    form->addRow(treeParameterLabel_, treeParameter_);
    form->addRow(treeSync_);
    v->addLayout(form);
    v->addStretch(1);

    connect(treeList_, &QListWidget::currentRowChanged, this, [this] {
        if (!updating_) refreshTree();
    });
    connect(addEntry_, &QPushButton::clicked, this, [this] {
        const int clipIndex = std::max(document_->clipIndex(), 0);
        document_->editMix(
            [&](AnimationMix& m) {
                BlendTreeEntry e;
                e.clip = clipIndex;
                e.threshold = m.tree.entries.empty() ? 0.0f : m.tree.entries.back().threshold + 1.0f;
                m.tree.entries.push_back(e);
                if (m.mode == MixMode::Clip) m.mode = MixMode::BlendTree;
            },
            "Edit Mix");
        treeList_->setCurrentRow(treeList_->count() - 1);
    });
    connect(removeEntry_, &QPushButton::clicked, this, [this] {
        const int i = treeIndex();
        if (i >= 0)
            document_->editMix([i](AnimationMix& m) { m.tree.entries.erase(m.tree.entries.begin() + i); },
                               "Edit Mix");
    });
    connect(entryClip_, &QComboBox::activated, this, [this](int index) {
        const int i = treeIndex();
        if (i >= 0)
            document_->editMix(
                [&](AnimationMix& m) { m.tree.entries[static_cast<std::size_t>(i)].clip = index; },
                "Edit Mix");
    });
    connect(entryThreshold_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const int i = treeIndex();
        if (i < 0 || updating_) return;
        document_->editMix(
            [&](AnimationMix& m) {
                m.tree.entries[static_cast<std::size_t>(i)].threshold = static_cast<float>(value);
            },
            "Edit Mix", "treeThreshold" + std::to_string(i));
    });
    connect(entryThreshold_, &QDoubleSpinBox::editingFinished, document_, &Document::endContinuousEdit);
    connect(treeParameter_, &QSlider::valueChanged, this, [this](int value) {
        if (updating_) return;
        document_->editMix(
            [&](AnimationMix& m) {
                if (m.tree.entries.empty()) return;
                float lo = m.tree.entries.front().threshold, hi = lo;
                for (const BlendTreeEntry& e : m.tree.entries) {
                    lo = std::min(lo, e.threshold);
                    hi = std::max(hi, e.threshold);
                }
                m.tree.parameter = lo + (hi - lo) * static_cast<float>(value) / 1000.0f;
            },
            "Edit Mix", "treeParameter");
    });
    connect(treeParameter_, &QSlider::sliderReleased, document_, &Document::endContinuousEdit);
    connect(treeSync_, &QCheckBox::clicked, this, [this](bool on) {
        document_->editMix([on](AnimationMix& m) { m.tree.syncTime = on; }, "Edit Mix");
    });
}

void MixPanel::buildRetargetTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    loadSource_ = button(page, "mixLoadSource");
    sourceLabel_ = new QLabel(page);
    sourceClip_ = combo(page, "mixSourceClip");
    autoMap_ = button(page, "mixAutoMap");
    mapTable_ = new QTableWidget(0, 2, page);
    mapTable_->setObjectName("mixMapTable");
    mapTable_->horizontalHeader()->setStretchLastSection(true);
    scaleTranslation_ = new QCheckBox(page);
    scaleTranslation_->setObjectName("mixScaleTranslation");
    scaleTranslation_->setChecked(true);
    retarget_ = button(page, "mixRetarget");
    v->addWidget(loadSource_);
    v->addWidget(sourceLabel_);
    v->addWidget(sourceClip_);
    v->addWidget(autoMap_);
    v->addWidget(mapTable_, 1);
    v->addWidget(scaleTranslation_);
    v->addWidget(retarget_);
    connect(loadSource_, &QPushButton::clicked, this, &MixPanel::loadRetargetFile);
    connect(autoMap_, &QPushButton::clicked, this, [this] {
        if (source_ && document_->hasCharacter()) {
            map_ = autoMapBones(source_->skeleton, document_->character()->skeleton);
            refreshRetarget();
            emit statusMessage(tr("%1 bones matched").arg(map_.pairs.size()));
        }
    });
    connect(retarget_, &QPushButton::clicked, this, &MixPanel::runRetarget);
}

// --- Rafraîchissement
// --------------------------------------------------------------------------------------------

int MixPanel::layerIndex() const { return layerList_->currentRow(); }
int MixPanel::stripIndex() const { return stripList_->currentRow(); }
int MixPanel::treeIndex() const { return treeList_->currentRow(); }

void MixPanel::fillClipCombo(QComboBox* c, int keep) {
    c->clear();
    if (document_->character()) {
        for (const AnimationClip& clip : document_->character()->clips)
            c->addItem(QString::fromStdString(clip.name));
    }
    c->setCurrentIndex(std::min(keep, c->count() - 1));
}

void MixPanel::refresh() {
    updating_ = true;
    mode_->setCurrentIndex(static_cast<int>(document_->mix().mode));
    mode_->setEnabled(document_->hasCharacter());
    updating_ = false;
    refreshLayers();
    refreshStrips();
    refreshTree();
    refreshRetarget();
}

void MixPanel::refreshLayers() {
    updating_ = true;
    const int keep = layerList_->currentRow();
    layerList_->clear();
    const auto& layers = document_->mix().layers;
    for (const AnimationLayer& l : layers) {
        layerList_->addItem(QStringLiteral("%1%2").arg(l.enabled ? QString() : QStringLiteral("(off) "),
                                                       QString::fromStdString(l.name)));
    }
    layerList_->setCurrentRow(std::min(keep, layerList_->count() - 1));
    const int i = layerIndex();
    const bool has = i >= 0;
    for (QWidget* w : std::initializer_list<QWidget*>{layerClip_, layerWeight_, layerMode_, layerSpeed_,
                                                      layerOffset_, layerEnabled_, maskFromSelection_,
                                                      clearMask_, removeLayer_, layerUp_, layerDown_}) {
        w->setEnabled(has);
    }
    fillClipCombo(layerClip_, has ? layers[static_cast<std::size_t>(i)].clip : 0);
    if (has) {
        const AnimationLayer& l = layers[static_cast<std::size_t>(i)];
        layerClip_->setCurrentIndex(l.clip);
        layerWeight_->setValue(static_cast<int>(std::lround(l.weight * 100.0f)));
        layerMode_->setCurrentIndex(l.mode == LayerMode::Additive ? 1 : 0);
        layerSpeed_->setValue(static_cast<double>(l.speed));
        layerOffset_->setValue(static_cast<double>(l.timeOffset));
        layerEnabled_->setChecked(l.enabled);
        layerMaskLabel_->setText(l.mask.empty()
                                     ? tr("Mask: whole body")
                                     : tr("Mask: %1 bone(s) and their children").arg(l.mask.size()));
    } else {
        layerMaskLabel_->setText(tr("Mask: whole body"));
    }
    updating_ = false;
}

void MixPanel::refreshStrips() {
    updating_ = true;
    const int keep = stripList_->currentRow();
    stripList_->clear();
    const auto& strips = document_->mix().strips;
    for (const NlaStrip& s : strips) {
        stripList_->addItem(
            QStringLiteral("%1 @ %2 s").arg(QString::fromStdString(s.name)).arg(s.start, 0, 'f', 2));
    }
    stripList_->setCurrentRow(std::min(keep, stripList_->count() - 1));
    const int i = stripIndex();
    const bool has = i >= 0;
    for (QWidget* w : std::initializer_list<QWidget*>{stripClip_, stripStart_, stripTrack_, stripRepeat_,
                                                      stripSpeed_, stripBlendIn_, stripBlendOut_,
                                                      stripWeight_, stripMode_, removeStrip_}) {
        w->setEnabled(has);
    }
    fillClipCombo(stripClip_, has ? strips[static_cast<std::size_t>(i)].clip : 0);
    fillClipCombo(fadeA_, std::max(fadeA_->currentIndex(), 0));
    fillClipCombo(fadeB_, std::max(fadeB_->currentIndex(), std::min(1, fadeB_->count() - 1)));
    fadeButton_->setEnabled(document_->hasCharacter() && fadeA_->count() > 1);
    if (has) {
        const NlaStrip& s = strips[static_cast<std::size_t>(i)];
        stripClip_->setCurrentIndex(s.clip);
        stripStart_->setValue(s.start);
        stripTrack_->setValue(s.track);
        stripRepeat_->setValue(static_cast<double>(s.repeat));
        stripSpeed_->setValue(static_cast<double>(s.speed));
        stripBlendIn_->setValue(s.blendIn);
        stripBlendOut_->setValue(s.blendOut);
        stripWeight_->setValue(static_cast<int>(std::lround(s.weight * 100.0f)));
        stripMode_->setCurrentIndex(s.mode == LayerMode::Additive ? 1 : 0);
    }
    updating_ = false;
}

void MixPanel::refreshTree() {
    updating_ = true;
    const int keep = treeList_->currentRow();
    treeList_->clear();
    const auto& tree = document_->mix().tree;
    const auto character = document_->character();
    for (const BlendTreeEntry& e : tree.entries) {
        const QString clipName =
            character && static_cast<std::size_t>(e.clip) < character->clips.size()
                ? QString::fromStdString(character->clips[static_cast<std::size_t>(e.clip)].name)
                : QStringLiteral("?");
        treeList_->addItem(
            QStringLiteral("%1  (%2)").arg(clipName).arg(static_cast<double>(e.threshold), 0, 'f', 2));
    }
    treeList_->setCurrentRow(std::min(keep, treeList_->count() - 1));
    const int i = treeIndex();
    const bool has = i >= 0;
    for (QWidget* w : std::initializer_list<QWidget*>{entryClip_, entryThreshold_, removeEntry_})
        w->setEnabled(has);
    treeParameter_->setEnabled(!tree.entries.empty());
    fillClipCombo(entryClip_, has ? tree.entries[static_cast<std::size_t>(i)].clip : 0);
    if (has) {
        entryClip_->setCurrentIndex(tree.entries[static_cast<std::size_t>(i)].clip);
        entryThreshold_->setValue(static_cast<double>(tree.entries[static_cast<std::size_t>(i)].threshold));
    }
    if (!tree.entries.empty()) {
        float lo = tree.entries.front().threshold, hi = lo;
        for (const BlendTreeEntry& e : tree.entries) {
            lo = std::min(lo, e.threshold);
            hi = std::max(hi, e.threshold);
        }
        treeParameter_->setValue(
            hi > lo ? static_cast<int>(std::lround((tree.parameter - lo) / (hi - lo) * 1000.0f)) : 0);
        treeParameterLabel_->setText(tr("%1: %2")
                                         .arg(QString::fromStdString(tree.parameterName))
                                         .arg(static_cast<double>(tree.parameter), 0, 'f', 2));
    } else {
        treeParameterLabel_->setText(tr("Parameter"));
    }
    treeSync_->setChecked(tree.syncTime);
    updating_ = false;
}

void MixPanel::setRetargetSource(std::shared_ptr<Character> source) {
    source_ = std::move(source);
    map_ = source_ && document_->hasCharacter()
               ? autoMapBones(source_->skeleton, document_->character()->skeleton)
               : BoneMap{};
    refreshRetarget();
}

void MixPanel::refreshRetarget() {
    updating_ = true;
    sourceClip_->clear();
    mapTable_->setRowCount(0);
    if (source_) {
        for (const AnimationClip& clip : source_->clips)
            sourceClip_->addItem(QString::fromStdString(clip.name));
        const auto character = document_->character();
        std::vector<BoneId> sourceBones;
        for (const BoneId id : source_->skeleton.order()) {
            if (!hasFlag(source_->skeleton.bone(id).flags, BoneFlags::Control)) sourceBones.push_back(id);
        }
        mapTable_->setRowCount(static_cast<int>(sourceBones.size()));
        for (int row = 0; row < static_cast<int>(sourceBones.size()); ++row) {
            const BoneId src = sourceBones[static_cast<std::size_t>(row)];
            mapTable_->setItem(
                row, 0, new QTableWidgetItem(QString::fromStdString(source_->skeleton.bone(src).name)));
            auto* target = new QComboBox(mapTable_);
            target->addItem(tr("(none)"), -1);
            if (character) {
                for (const BoneId id : character->skeleton.order()) {
                    if (!hasFlag(character->skeleton.bone(id).flags, BoneFlags::Control)) {
                        target->addItem(QString::fromStdString(character->skeleton.bone(id).name),
                                        static_cast<int>(id));
                    }
                }
            }
            const BoneId mapped = map_.target(src);
            target->setCurrentIndex(
                std::max(0, target->findData(mapped == kNoBone ? -1 : static_cast<int>(mapped))));
            connect(target, &QComboBox::activated, this, [this, src, target](int index) {
                const int data = target->itemData(index).toInt();
                map_.pairs.erase(std::remove_if(map_.pairs.begin(), map_.pairs.end(),
                                                [src](const auto& p) { return p.first == src; }),
                                 map_.pairs.end());
                if (data >= 0) map_.pairs.emplace_back(src, static_cast<BoneId>(data));
            });
            mapTable_->setCellWidget(row, 1, target);
        }
        sourceLabel_->setText(tr("Source: %1 (%2 bones, %3 clips), %4 matched")
                                  .arg(QString::fromStdString(source_->name.empty() ? "?" : source_->name))
                                  .arg(source_->skeleton.boneCount())
                                  .arg(source_->clips.size())
                                  .arg(map_.pairs.size()));
    } else {
        sourceLabel_->setText(tr("No source animation loaded"));
    }
    retarget_->setEnabled(source_ && document_->hasCharacter() && !source_->clips.empty());
    autoMap_->setEnabled(source_ && document_->hasCharacter());
    updating_ = false;
}

void MixPanel::loadRetargetFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load the source animation"), QString(),
                                                      tr("glTF 2.0 (*.gltf *.glb)"));
    if (path.isEmpty()) return;
    auto imported = io::importGltf(std::filesystem::path(path.toStdWString()));
    if (!imported) {
        emit statusMessage(tr("Import failed: %1").arg(QString::fromStdString(imported.error().message)));
        return;
    }
    setRetargetSource(std::make_shared<Character>(std::move(imported.value())));
}

void MixPanel::runRetarget() {
    if (!source_) return;
    RetargetOptions options;
    options.scaleTranslation = scaleTranslation_->isChecked();
    const auto result = document_->retargetClipFrom(
        *source_, static_cast<std::size_t>(std::max(sourceClip_->currentIndex(), 0)), map_, options);
    emit statusMessage(result ? tr("Retargeted clip added") : QString::fromStdString(result.error().message));
}

void MixPanel::retranslate() {
    modeLabel_->setText(tr("Playback"));
    const QStringList modes{tr("Single clip"), tr("Layers"), tr("NLA sequencer"), tr("Blend tree")};
    for (int i = 0; i < 4; ++i) mode_->setItemText(i, modes[i]);
    tabs_->setTabText(0, tr("Layers"));
    tabs_->setTabText(1, tr("NLA"));
    tabs_->setTabText(2, tr("Blend Tree"));
    tabs_->setTabText(3, tr("Retarget"));
    addLayer_->setText(tr("Add"));
    removeLayer_->setText(tr("Remove"));
    layerUp_->setText(tr("Up"));
    layerDown_->setText(tr("Down"));
    layerClipLabel_->setText(tr("Clip"));
    layerWeightLabel_->setText(tr("Weight"));
    layerModeLabel_->setText(tr("Blend"));
    layerMode_->setItemText(0, tr("Override"));
    layerMode_->setItemText(1, tr("Additive"));
    layerSpeedLabel_->setText(tr("Speed"));
    layerOffsetLabel_->setText(tr("Time offset"));
    layerEnabled_->setText(tr("Enabled"));
    maskFromSelection_->setText(tr("Mask = Selected Bones"));
    maskFromSelection_->setToolTip(
        tr("The layer only affects the selected bones and their children (e.g. the upper body)"));
    clearMask_->setText(tr("Clear Mask"));
    addStrip_->setText(tr("Add Strip"));
    removeStrip_->setText(tr("Remove Strip"));
    const QStringList labels{tr("Clip"),        tr("Start (s)"),    tr("Track"),  tr("Repeat"), tr("Speed"),
                             tr("Fade in (s)"), tr("Fade out (s)"), tr("Weight"), tr("Blend")};
    for (int i = 0; i < 9; ++i) stripLabels_[i]->setText(labels[i]);
    stripMode_->setItemText(0, tr("Override"));
    stripMode_->setItemText(1, tr("Additive"));
    fadeLabel_->setText(tr("Crossfade"));
    fadeButton_->setText(tr("Create Crossfade A -> B"));
    fadeButton_->setToolTip(tr("Two overlapping strips: B fades in over the end of A"));
    addEntry_->setText(tr("Add Clip"));
    removeEntry_->setText(tr("Remove Clip"));
    entryClipLabel_->setText(tr("Clip"));
    entryThresholdLabel_->setText(tr("Threshold"));
    treeSync_->setText(tr("Synchronize cycles"));
    loadSource_->setText(tr("Load Source Animation..."));
    autoMap_->setText(tr("Auto-Match Bones"));
    autoMap_->setToolTip(tr("Match bones by common humanoid names (hips, spine, head, arm, leg...)"));
    scaleTranslation_->setText(tr("Scale translations to the new proportions"));
    retarget_->setText(tr("Retarget Clip"));
    mapTable_->setHorizontalHeaderLabels({tr("Source bone"), tr("Target bone")});
    refresh();
}

void MixPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
