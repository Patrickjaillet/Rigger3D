// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/DeformPanel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <glm/gtc/constants.hpp>

#include "core/deform/morph.hpp"
#include "editor/Document.hpp"

namespace rigger::editor {

namespace {

QPushButton* button(QWidget* parent, const char* name) {
    auto* b = new QPushButton(parent);
    b->setObjectName(name);
    return b;
}

QDoubleSpinBox* dspin(QWidget* parent, const char* name, double lo, double hi, double step, int decimals) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(lo, hi);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    return box;
}

QListWidget* list(QWidget* parent, const char* name) {
    auto* l = new QListWidget(parent);
    l->setObjectName(name);
    l->setMaximumHeight(90);
    return l;
}

QLabel* hint(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    return l;
}

}  // namespace

DeformPanel::DeformPanel(Document* document, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName("deformPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("deformTabs");
    for (int i = 0; i < 5; ++i) {
        auto* scroll = new QScrollArea(tabs_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* page = new QWidget(scroll);
        scroll->setWidget(page);
        tabs_->addTab(scroll, QString());
        if (i == 0) buildMorphTab(page);
        if (i == 1) buildTwistTab(page);
        if (i == 2) buildConstraintTab(page);
        if (i == 3) buildSpringTab(page);
        if (i == 4) buildRigTab(page);
    }
    root->addWidget(tabs_);
    for (auto signal : {&Document::deformChanged, &Document::characterChanged, &Document::selectionChanged,
                        &Document::animationChanged}) {
        connect(document_, signal, this, [this] { refresh(); });
    }
    connect(document_, &Document::poseChanged, this, [this] { refreshMorphs(); });
    retranslate();
    refresh();
}

// --- Blend shapes
// ------------------------------------------------------------------------------------------------------

void DeformPanel::buildMorphTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    morphEmpty_ = hint(page);
    v->addWidget(morphEmpty_);
    morphHost_ = new QWidget(page);
    morphHost_->setObjectName("morphHost");
    new QVBoxLayout(morphHost_);
    v->addWidget(morphHost_);
    driverHint_ = hint(page);
    v->addWidget(driverHint_);
    driverList_ = list(page, "morphDriverList");
    v->addWidget(driverList_);
    auto* form = new QFormLayout;
    driverAxis_ = new QComboBox(page);
    driverAxis_->setObjectName("morphDriverAxis");
    for (const char* axis : {"X", "Y", "Z"}) driverAxis_->addItem(axis);
    driverFrom_ = dspin(page, "morphDriverFrom", -360, 360, 5, 1);
    driverTo_ = dspin(page, "morphDriverTo", -360, 360, 5, 1);
    driverTo_->setValue(90);
    form->addRow(tr("Axis"), driverAxis_);
    form->addRow(tr("Angle for weight 0 (deg)"), driverFrom_);
    form->addRow(tr("Angle for weight 1 (deg)"), driverTo_);
    v->addLayout(form);
    addDriver_ = button(page, "morphAddDriver");
    removeDriver_ = button(page, "morphRemoveDriver");
    v->addWidget(addDriver_);
    v->addWidget(removeDriver_);
    v->addStretch(1);

    connect(addDriver_, &QPushButton::clicked, this, [this] {
        const BoneId bone = document_->activeBone();
        if (selectedMesh_ < 0 || selectedTarget_ < 0 || bone == kNoBone) {
            emit statusMessage(tr("Select a blend shape here and a bone in the hierarchy first"));
            return;
        }
        auto drivers = document_->character()
                           ->meshes[static_cast<std::size_t>(selectedMesh_)]
                           .morphTargets[static_cast<std::size_t>(selectedTarget_)]
                           .drivers;
        MorphDriver d;
        d.bone = bone;
        d.axis = glm::vec3(driverAxis_->currentIndex() == 0, driverAxis_->currentIndex() == 1,
                           driverAxis_->currentIndex() == 2);
        d.angle0 = glm::radians(static_cast<float>(driverFrom_->value()));
        d.angle1 = glm::radians(static_cast<float>(driverTo_->value()));
        drivers.push_back(d);
        document_->editMorphDrivers(static_cast<std::size_t>(selectedMesh_),
                                    static_cast<std::size_t>(selectedTarget_), drivers);
    });
    connect(removeDriver_, &QPushButton::clicked, this, [this] {
        const int row = driverList_->currentRow();
        if (selectedMesh_ < 0 || selectedTarget_ < 0 || row < 0) return;
        auto drivers = document_->character()
                           ->meshes[static_cast<std::size_t>(selectedMesh_)]
                           .morphTargets[static_cast<std::size_t>(selectedTarget_)]
                           .drivers;
        if (static_cast<std::size_t>(row) < drivers.size()) drivers.erase(drivers.begin() + row);
        document_->editMorphDrivers(static_cast<std::size_t>(selectedMesh_),
                                    static_cast<std::size_t>(selectedTarget_), drivers);
    });
}

void DeformPanel::refreshMorphs() {
    if (updating_) return;
    // Met à jour les valeurs des sliders sans reconstruire (lecture / animation).
    const auto character = document_->character();
    if (!character) return;
    updating_ = true;
    for (QSlider* slider : morphHost_->findChildren<QSlider*>()) {
        const int mesh = slider->property("mesh").toInt(), target = slider->property("target").toInt();
        if (static_cast<std::size_t>(mesh) >= character->meshes.size() ||
            static_cast<std::size_t>(target) >=
                character->meshes[static_cast<std::size_t>(mesh)].morphTargets.size()) {
            continue;
        }
        const auto weights = morphWeights(character->meshes[static_cast<std::size_t>(mesh)],
                                          character->skeleton, document_->pose());
        slider->setValue(static_cast<int>(
            std::lround(std::clamp(weights[static_cast<std::size_t>(target)], 0.0f, 1.0f) * 100.0f)));
    }
    updating_ = false;
}

// --- Twist
// ----------------------------------------------------------------------------------------------------------------

void DeformPanel::buildTwistTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    twistHint_ = hint(page);
    v->addWidget(twistHint_);
    twistCount_ = new QSpinBox(page);
    twistCount_->setObjectName("twistCount");
    twistCount_->setRange(1, 8);
    twistCount_->setValue(2);
    createTwist_ = button(page, "twistCreate");
    twistList_ = list(page, "twistList");
    removeTwist_ = button(page, "twistRemove");
    v->addWidget(twistCount_);
    v->addWidget(createTwist_);
    v->addWidget(twistList_);
    v->addWidget(removeTwist_);
    v->addStretch(1);
    connect(createTwist_, &QPushButton::clicked, this, [this] {
        const BoneId forearm = document_->activeBone();
        if (forearm == kNoBone || document_->skeleton()->children(forearm).empty()) {
            emit statusMessage(
                tr("Select the forearm (or shin) bone: its first child is the hand (or foot)"));
            return;
        }
        const BoneId hand = document_->skeleton()->children(forearm).front();
        const auto result = document_->createTwistSetup(forearm, hand, twistCount_->value());
        if (!result) emit statusMessage(QString::fromStdString(result.error().message));
    });
    connect(removeTwist_, &QPushButton::clicked, this, [this] {
        const int row = twistList_->currentRow();
        if (row >= 0)
            document_->editDeform([row](DeformRig& d) { d.twists.erase(d.twists.begin() + row); },
                                  "Edit Deform");
    });
}

// --- Contraintes
// -----------------------------------------------------------------------------------------------------------

int DeformPanel::constraintIndex() const { return constraintList_->currentRow(); }
int DeformPanel::springIndex() const { return springList_->currentRow(); }

void DeformPanel::buildConstraintTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    constraintHint_ = hint(page);
    v->addWidget(constraintHint_);
    constraintKind_ = new QComboBox(page);
    constraintKind_->setObjectName("constraintKind");
    for (int i = 0; i < 4; ++i) constraintKind_->addItem(QString(), i);
    addConstraint_ = button(page, "constraintAdd");
    constraintList_ = list(page, "constraintList");
    influenceLabel_ = new QLabel(page);
    constraintInfluence_ = new QSlider(Qt::Horizontal, page);
    constraintInfluence_->setObjectName("constraintInfluence");
    constraintInfluence_->setRange(0, 100);
    constraintEnabled_ = new QCheckBox(page);
    constraintEnabled_->setObjectName("constraintEnabled");
    removeConstraint_ = button(page, "constraintRemove");
    v->addWidget(constraintKind_);
    v->addWidget(addConstraint_);
    v->addWidget(constraintList_);
    v->addWidget(influenceLabel_);
    v->addWidget(constraintInfluence_);
    v->addWidget(constraintEnabled_);
    v->addWidget(removeConstraint_);
    v->addStretch(1);
    connect(addConstraint_, &QPushButton::clicked, this, [this] {
        const auto& selection = document_->selection();
        if (selection.empty()) {
            emit statusMessage(
                tr("Select the constrained bone first (then the target bone with Ctrl+click)"));
            return;
        }
        Constraint c;
        c.kind = static_cast<ConstraintKind>(constraintKind_->currentData().toInt());
        c.bone = selection.front();
        c.target = selection.size() > 1 ? selection[1] : kNoBone;
        c.name = constraintKind_->currentText().toStdString();
        const auto result = document_->addConstraint(c);
        if (!result) emit statusMessage(QString::fromStdString(result.error().message));
    });
    connect(constraintList_, &QListWidget::currentRowChanged, this, [this] {
        if (!updating_) refreshLists();
    });
    connect(constraintInfluence_, &QSlider::valueChanged, this, [this](int value) {
        const int i = constraintIndex();
        if (i < 0 || updating_) return;
        document_->editDeform(
            [&](DeformRig& d) {
                d.constraints[static_cast<std::size_t>(i)].influence = static_cast<float>(value) / 100.0f;
            },
            "Edit Deform", "influence" + std::to_string(i));
    });
    connect(constraintInfluence_, &QSlider::sliderReleased, document_, &Document::endContinuousEdit);
    connect(constraintEnabled_, &QCheckBox::clicked, this, [this](bool on) {
        const int i = constraintIndex();
        if (i >= 0)
            document_->editDeform(
                [&](DeformRig& d) { d.constraints[static_cast<std::size_t>(i)].enabled = on; },
                "Edit Deform");
    });
    connect(removeConstraint_, &QPushButton::clicked, this, [this] {
        const int i = constraintIndex();
        if (i >= 0)
            document_->editDeform([i](DeformRig& d) { d.constraints.erase(d.constraints.begin() + i); },
                                  "Edit Deform");
    });
}

// --- Springs
// --------------------------------------------------------------------------------------------------------------

void DeformPanel::buildSpringTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    springsGlobal_ = new QCheckBox(page);
    springsGlobal_->setObjectName("springsGlobal");
    v->addWidget(springsGlobal_);
    springBones_ = new QSpinBox(page);
    springBones_->setObjectName("springBones");
    springBones_->setRange(1, 32);
    springBones_->setValue(3);
    createSpring_ = button(page, "springCreate");
    springList_ = list(page, "springList");
    v->addWidget(springBones_);
    v->addWidget(createSpring_);
    v->addWidget(springList_);
    auto* form = new QFormLayout;
    springStiffness_ = dspin(page, "springStiffness", 0.0, 1.0, 0.05, 2);
    springDamping_ = dspin(page, "springDamping", 0.0, 1.0, 0.05, 2);
    springGravity_ = dspin(page, "springGravity", -5.0, 5.0, 0.1, 2);
    springEnabled_ = new QCheckBox(page);
    springEnabled_->setObjectName("springEnabled");
    stiffnessLabel_ = new QLabel(page);
    dampingLabel_ = new QLabel(page);
    gravityLabel_ = new QLabel(page);
    form->addRow(stiffnessLabel_, springStiffness_);
    form->addRow(dampingLabel_, springDamping_);
    form->addRow(gravityLabel_, springGravity_);
    form->addRow(springEnabled_);
    v->addLayout(form);
    removeSpring_ = button(page, "springRemove");
    v->addWidget(removeSpring_);
    colliderRadius_ = dspin(page, "colliderRadius", 0.005, 5.0, 0.01, 3);
    colliderRadius_->setValue(0.1);
    addCollider_ = button(page, "colliderAdd");
    colliderCount_ = new QLabel(page);
    v->addWidget(colliderRadius_);
    v->addWidget(addCollider_);
    v->addWidget(colliderCount_);
    auto* bake = new QHBoxLayout;
    bakeLabel_ = new QLabel(page);
    bakeFirst_ = new QSpinBox(page);
    bakeFirst_->setObjectName("springBakeFirst");
    bakeFirst_->setRange(0, 100000);
    bakeLast_ = new QSpinBox(page);
    bakeLast_->setObjectName("springBakeLast");
    bakeLast_->setRange(0, 100000);
    bakeLast_->setValue(60);
    bake->addWidget(bakeLabel_);
    bake->addWidget(bakeFirst_);
    bake->addWidget(bakeLast_);
    v->addLayout(bake);
    bakeDisable_ = new QCheckBox(page);
    bakeDisable_->setChecked(true);
    bakeSprings_ = button(page, "springBake");
    v->addWidget(bakeDisable_);
    v->addWidget(bakeSprings_);
    v->addStretch(1);

    connect(springsGlobal_, &QCheckBox::clicked, this, [this](bool on) {
        document_->editDeform([on](DeformRig& d) { d.springsEnabled = on; }, "Edit Deform");
    });
    connect(createSpring_, &QPushButton::clicked, this, [this] {
        const BoneId root = document_->activeBone();
        if (root == kNoBone) {
            emit statusMessage(tr("Select the first bone of the hair / tail chain"));
            return;
        }
        const auto result = document_->createSpringChain(root, springBones_->value());
        if (!result) emit statusMessage(QString::fromStdString(result.error().message));
    });
    connect(springList_, &QListWidget::currentRowChanged, this, [this] {
        if (!updating_) refreshLists();
    });
    const auto bind = [this](QDoubleSpinBox* box, const char* key,
                             std::function<void(SpringChain&, float)> set) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, key, set](double value) {
            const int i = springIndex();
            if (i < 0 || updating_) return;
            document_->editDeform(
                [&](DeformRig& d) { set(d.springs[static_cast<std::size_t>(i)], static_cast<float>(value)); },
                "Edit Deform", std::string(key) + std::to_string(i));
        });
        connect(box, &QDoubleSpinBox::editingFinished, document_, &Document::endContinuousEdit);
    };
    bind(springStiffness_, "stiffness", [](SpringChain& s, float v) { s.stiffness = v; });
    bind(springDamping_, "damping", [](SpringChain& s, float v) { s.damping = v; });
    bind(springGravity_, "gravity", [](SpringChain& s, float v) { s.gravity = v; });
    connect(springEnabled_, &QCheckBox::clicked, this, [this](bool on) {
        const int i = springIndex();
        if (i >= 0)
            document_->editDeform([&](DeformRig& d) { d.springs[static_cast<std::size_t>(i)].enabled = on; },
                                  "Edit Deform");
    });
    connect(removeSpring_, &QPushButton::clicked, this, [this] {
        const int i = springIndex();
        if (i >= 0)
            document_->editDeform([i](DeformRig& d) { d.springs.erase(d.springs.begin() + i); },
                                  "Edit Deform");
    });
    connect(addCollider_, &QPushButton::clicked, this, [this] {
        const BoneId bone = document_->activeBone();
        if (bone == kNoBone) {
            emit statusMessage(tr("Select the bone that carries the collision sphere (head, body...)"));
            return;
        }
        document_->addSpringCollider(bone, glm::vec3(0.0f), static_cast<float>(colliderRadius_->value()));
    });
    connect(bakeSprings_, &QPushButton::clicked, this, [this] {
        const auto result =
            document_->bakeSprings(bakeFirst_->value(), bakeLast_->value(), bakeDisable_->isChecked());
        emit statusMessage(result ? tr("Springs baked into rotation keys")
                                  : QString::fromStdString(result.error().message));
    });
}

// --- Auto-rig
// -------------------------------------------------------------------------------------------------------------

void DeformPanel::buildRigTab(QWidget* page) {
    auto* v = new QVBoxLayout(page);
    rigHint_ = hint(page);
    v->addWidget(rigHint_);
    landmarks_ = new QTableWidget(kLandmarkCount, 3, page);
    landmarks_->setObjectName("rigLandmarks");
    landmarks_->setMinimumHeight(220);
    landmarks_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int row = 0; row < kLandmarkCount; ++row) {
        for (int col = 0; col < 3; ++col) {
            auto* box = new QDoubleSpinBox(landmarks_);
            box->setRange(-1000.0, 1000.0);
            box->setDecimals(3);
            box->setSingleStep(0.05);
            landmarks_->setCellWidget(row, col, box);
        }
    }
    v->addWidget(landmarks_);
    guess_ = button(page, "rigGuess");
    skinMethod_ = new QComboBox(page);
    skinMethod_->setObjectName("rigSkinMethod");
    skinMethod_->addItem(QString(), 0);
    skinMethod_->addItem(QString(), 1);
    withIk_ = new QCheckBox(page);
    withIk_->setObjectName("rigWithIk");
    withIk_->setChecked(true);
    generate_ = button(page, "rigGenerate");
    v->addWidget(guess_);
    v->addWidget(skinMethod_);
    v->addWidget(withIk_);
    v->addWidget(generate_);
    v->addStretch(1);
    connect(guess_, &QPushButton::clicked, this, [this] {
        if (document_->hasCharacter()) refreshLandmarks(document_->guessCharacterLandmarks());
    });
    connect(generate_, &QPushButton::clicked, this, [this] {
        AutoSkinOptions options;
        options.method = skinMethod_->currentData().toInt() == 1 ? AutoSkinMethod::HeatDiffusion
                                                                 : AutoSkinMethod::Distance;
        const auto result = document_->autoRigHumanoid(readLandmarks(), options, withIk_->isChecked());
        emit statusMessage(result ? tr("Humanoid rig generated")
                                  : QString::fromStdString(result.error().message));
    });
}

void DeformPanel::refreshLandmarks(const Landmarks& landmarks) {
    for (int row = 0; row < kLandmarkCount; ++row) {
        for (int col = 0; col < 3; ++col) {
            static_cast<QDoubleSpinBox*>(landmarks_->cellWidget(row, col))
                ->setValue(static_cast<double>(landmarks[static_cast<std::size_t>(row)][col]));
        }
    }
}

Landmarks DeformPanel::readLandmarks() const {
    Landmarks l{};
    for (int row = 0; row < kLandmarkCount; ++row) {
        for (int col = 0; col < 3; ++col) {
            l[static_cast<std::size_t>(row)][col] =
                static_cast<float>(static_cast<QDoubleSpinBox*>(landmarks_->cellWidget(row, col))->value());
        }
    }
    return l;
}

// --- Rafraîchissement
// --------------------------------------------------------------------------------------------------------

void DeformPanel::refresh() {
    const auto character = document_->character();
    updating_ = true;
    // Blend shapes : un slider par cible.
    qDeleteAll(morphHost_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
    auto* layout = static_cast<QVBoxLayout*>(morphHost_->layout());
    bool any = false;
    if (character) {
        for (std::size_t m = 0; m < character->meshes.size(); ++m) {
            for (std::size_t t = 0; t < character->meshes[m].morphTargets.size(); ++t) {
                any = true;
                auto* row = new QWidget(morphHost_);
                auto* h = new QHBoxLayout(row);
                h->setContentsMargins(0, 0, 0, 0);
                auto* name =
                    new QPushButton(QString::fromStdString(character->meshes[m].morphTargets[t].name), row);
                name->setFlat(true);
                name->setCheckable(true);
                name->setChecked(static_cast<int>(m) == selectedMesh_ &&
                                 static_cast<int>(t) == selectedTarget_);
                auto* slider = new QSlider(Qt::Horizontal, row);
                slider->setRange(0, 100);
                slider->setProperty("mesh", static_cast<int>(m));
                slider->setProperty("target", static_cast<int>(t));
                slider->setObjectName(QStringLiteral("morphSlider_%1_%2").arg(m).arg(t));
                h->addWidget(name, 1);
                h->addWidget(slider, 2);
                layout->addWidget(row);
                row->show();
                connect(slider, &QSlider::valueChanged, this, [this, m, t](int value) {
                    if (!updating_) document_->setMorphWeight(m, t, static_cast<float>(value) / 100.0f);
                });
                connect(slider, &QSlider::sliderReleased, document_, &Document::endContinuousEdit);
                connect(name, &QPushButton::clicked, this, [this, m, t] {
                    selectedMesh_ = static_cast<int>(m);
                    selectedTarget_ = static_cast<int>(t);
                    refresh();
                });
            }
        }
    }
    morphEmpty_->setVisible(!any);
    refreshLists();
    refreshMorphs();
    updating_ = true;
    const bool has = character != nullptr;
    createTwist_->setEnabled(has);
    createSpring_->setEnabled(has);
    addConstraint_->setEnabled(has);
    generate_->setEnabled(has && !character->meshes.empty());
    guess_->setEnabled(has && !character->meshes.empty());
    springsGlobal_->setChecked(document_->deform().springsEnabled);
    updating_ = false;
}

void DeformPanel::refreshLists() {
    updating_ = true;
    const DeformRig& rig = document_->deform();
    const auto character = document_->character();
    const auto boneName = [&](BoneId id) {
        return character && character->skeleton.contains(id)
                   ? QString::fromStdString(character->skeleton.bone(id).name)
                   : QStringLiteral("?");
    };
    // Drivers de la blend shape sélectionnée.
    driverList_->clear();
    const bool target = character && selectedMesh_ >= 0 &&
                        static_cast<std::size_t>(selectedMesh_) < character->meshes.size() &&
                        selectedTarget_ >= 0 &&
                        static_cast<std::size_t>(selectedTarget_) <
                            character->meshes[static_cast<std::size_t>(selectedMesh_)].morphTargets.size();
    if (target) {
        for (const MorphDriver& d : character->meshes[static_cast<std::size_t>(selectedMesh_)]
                                        .morphTargets[static_cast<std::size_t>(selectedTarget_)]
                                        .drivers) {
            driverList_->addItem(tr("%1: %2° -> %3°")
                                     .arg(boneName(d.bone))
                                     .arg(static_cast<double>(glm::degrees(d.angle0)), 0, 'f', 0)
                                     .arg(static_cast<double>(glm::degrees(d.angle1)), 0, 'f', 0));
        }
    }
    addDriver_->setEnabled(target);
    removeDriver_->setEnabled(target);
    const auto fill = [](QListWidget* l, const QStringList& items) {
        const int keep = l->currentRow();
        l->clear();
        l->addItems(items);
        l->setCurrentRow(std::min(keep, l->count() - 1));
    };
    QStringList twists, constraints, springs;
    for (const TwistChain& t : rig.twists)
        twists << tr("%1 (%2 bones)").arg(boneName(t.source)).arg(t.bones.size());
    for (const Constraint& c : rig.constraints)
        constraints << QStringLiteral("%1 -> %2")
                           .arg(boneName(c.bone),
                                c.kind == ConstraintKind::Limit ? QStringLiteral("-") : boneName(c.target));
    for (const SpringChain& s : rig.springs)
        springs << tr("%1 (%2 bones)").arg(QString::fromStdString(s.name)).arg(s.bones.size());
    fill(twistList_, twists);
    fill(constraintList_, constraints);
    fill(springList_, springs);
    removeTwist_->setEnabled(twistList_->currentRow() >= 0);
    const int ci = constraintIndex();
    constraintInfluence_->setEnabled(ci >= 0);
    constraintEnabled_->setEnabled(ci >= 0);
    removeConstraint_->setEnabled(ci >= 0);
    if (ci >= 0) {
        const Constraint& c = rig.constraints[static_cast<std::size_t>(ci)];
        constraintInfluence_->setValue(static_cast<int>(std::lround(c.influence * 100.0f)));
        constraintEnabled_->setChecked(c.enabled);
    }
    const int si = springIndex();
    for (QWidget* w : std::initializer_list<QWidget*>{springStiffness_, springDamping_, springGravity_,
                                                      springEnabled_, removeSpring_})
        w->setEnabled(si >= 0);
    if (si >= 0) {
        const SpringChain& s = rig.springs[static_cast<std::size_t>(si)];
        springStiffness_->setValue(static_cast<double>(s.stiffness));
        springDamping_->setValue(static_cast<double>(s.damping));
        springGravity_->setValue(static_cast<double>(s.gravity));
        springEnabled_->setChecked(s.enabled);
    }
    colliderCount_->setText(tr("Colliders: %1").arg(rig.colliders.size()));
    bakeSprings_->setEnabled(!rig.springs.empty() && document_->clip() != nullptr);
    updating_ = false;
}

void DeformPanel::retranslate() {
    tabs_->setTabText(0, tr("Blend Shapes"));
    tabs_->setTabText(1, tr("Twist"));
    tabs_->setTabText(2, tr("Constraints"));
    tabs_->setTabText(3, tr("Springs"));
    tabs_->setTabText(4, tr("Auto-Rig"));
    morphEmpty_->setText(
        tr("This character has no blend shapes. Import a glTF that contains morph targets."));
    driverHint_->setText(
        tr("Corrective shape: click a blend shape name above, select a joint in the hierarchy, then add a "
           "driver. The shape turns on with the joint angle."));
    addDriver_->setText(tr("Drive Selected Shape from Selected Bone"));
    removeDriver_->setText(tr("Remove Driver"));
    twistHint_->setText(
        tr("Select the forearm (or shin): twist bones are created between it and its child (hand / foot) and "
           "share the twist of that child."));
    twistCount_->setToolTip(tr("Number of twist bones"));
    createTwist_->setText(tr("Create Twist Bones"));
    removeTwist_->setText(tr("Remove Twist Setup"));
    constraintHint_->setText(
        tr("Select the constrained bone, then Ctrl+click the target bone (not needed for Limit)."));
    const QStringList kinds{tr("Copy Transform"), tr("Limit"), tr("Child Of"), tr("Track To")};
    for (int i = 0; i < 4; ++i) constraintKind_->setItemText(i, kinds[i]);
    addConstraint_->setText(tr("Add Constraint"));
    influenceLabel_->setText(tr("Influence"));
    constraintEnabled_->setText(tr("Enabled"));
    removeConstraint_->setText(tr("Remove Constraint"));
    springsGlobal_->setText(tr("Simulate spring bones"));
    springsGlobal_->setToolTip(tr("Turn the whole secondary physics on or off"));
    springBones_->setToolTip(tr("Number of bones in the chain"));
    createSpring_->setText(tr("Create Spring Chain from Selected Bone"));
    stiffnessLabel_->setText(tr("Stiffness"));
    dampingLabel_->setText(tr("Damping"));
    gravityLabel_->setText(tr("Gravity"));
    springEnabled_->setText(tr("Enabled"));
    removeSpring_->setText(tr("Remove Chain"));
    colliderRadius_->setToolTip(tr("Collision sphere radius (m)"));
    addCollider_->setText(tr("Add Collision Sphere on Selected Bone"));
    bakeLabel_->setText(tr("Bake frames"));
    bakeDisable_->setText(tr("Disable simulation afterwards"));
    bakeSprings_->setText(tr("Bake Springs to Keys"));
    bakeSprings_->setToolTip(
        tr("Turn the simulation into rotation keys (needed to export or play it elsewhere)"));
    rigHint_->setText(
        tr("Place about 20 landmarks on the character (X / Y / Z in metres, character facing +Z, left side "
           "at +X), or guess them from the mesh, then generate a skeleton with skinning and IK."));
    landmarks_->setHorizontalHeaderLabels({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
    QStringList names;
    for (int i = 0; i < kLandmarkCount; ++i) names << tr(landmarkName(static_cast<Landmark>(i)));
    landmarks_->setVerticalHeaderLabels(names);
    guess_->setText(tr("Guess Landmarks from Mesh"));
    skinMethod_->setItemText(0, tr("Skinning: distance"));
    skinMethod_->setItemText(1, tr("Skinning: heat diffusion"));
    withIk_->setText(tr("Create humanoid IK"));
    generate_->setText(tr("Generate Humanoid Rig"));
    generate_->setToolTip(tr("Replaces the skeleton, the weights, the clips and the IK of this character"));
}

void DeformPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
