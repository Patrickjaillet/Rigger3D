// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/IKPanel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <glm/gtc/constants.hpp>

#include "editor/Document.hpp"

namespace rigger::editor {

namespace {
const IKControl* controlAt(const Document* document, int index) {
    const auto& controls = document->ikControls();
    return index >= 0 && static_cast<std::size_t>(index) < controls.size()
               ? &controls[static_cast<std::size_t>(index)]
               : nullptr;
}
}  // namespace

IKPanel::IKPanel(Document* document, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName("ikPanel");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    // --- Création ---
    auto* createRow = new QHBoxLayout;
    boneCount_ = new QSpinBox(this);
    boneCount_->setObjectName("ikBoneCount");
    boneCount_->setRange(1, 32);
    boneCount_->setValue(2);
    solver_ = new QComboBox(this);
    solver_->setObjectName("ikSolver");
    solver_->addItem(QString(), static_cast<int>(IKSolver::TwoBone));
    solver_->addItem(QString(), static_cast<int>(IKSolver::FABRIK));
    solver_->addItem(QString(), static_cast<int>(IKSolver::CCD));
    withPole_ = new QCheckBox(this);
    withPole_->setObjectName("ikWithPole");
    withPole_->setChecked(true);
    createRow->addWidget(boneCount_);
    createRow->addWidget(solver_, 1);
    createRow->addWidget(withPole_);
    root->addLayout(createRow);
    createButton_ = new QPushButton(this);
    createButton_->setObjectName("ikCreate");
    lookAtButton_ = new QPushButton(this);
    lookAtButton_->setObjectName("ikLookAt");
    humanoidButton_ = new QPushButton(this);
    humanoidButton_->setObjectName("ikHumanoid");
    root->addWidget(createButton_);
    auto* presetRow = new QHBoxLayout;
    presetRow->addWidget(lookAtButton_);
    presetRow->addWidget(humanoidButton_);
    root->addLayout(presetRow);

    // --- Liste + réglages ---
    list_ = new QListWidget(this);
    list_->setObjectName("ikList");
    list_->setMaximumHeight(110);
    root->addWidget(list_);
    settingsBox_ = new QGroupBox(this);
    auto* form = new QFormLayout(settingsBox_);
    toggleButton_ = new QPushButton(this);
    toggleButton_->setObjectName("ikToggle");
    weight_ = new QSlider(Qt::Horizontal, this);
    weight_->setObjectName("ikWeight");
    weight_->setRange(0, 100);
    weightLabel_ = new QLabel(this);
    controlSolver_ = new QComboBox(this);
    controlSolver_->setObjectName("ikControlSolver");
    for (int i = 0; i < 3; ++i) controlSolver_->addItem(QString(), i);
    solverLabel_ = new QLabel(this);
    orientation_ = new QComboBox(this);
    orientation_->setObjectName("ikOrientation");
    for (int i = 0; i < 3; ++i) orientation_->addItem(QString(), i);
    orientationLabel_ = new QLabel(this);
    twist_ = new QDoubleSpinBox(this);
    twist_->setObjectName("ikTwist");
    twist_->setRange(-180.0, 180.0);
    twist_->setSuffix(QStringLiteral(" °"));
    twistLabel_ = new QLabel(this);
    removeButton_ = new QPushButton(this);
    removeButton_->setObjectName("ikRemove");
    form->addRow(toggleButton_);
    form->addRow(weightLabel_, weight_);
    form->addRow(solverLabel_, controlSolver_);
    form->addRow(orientationLabel_, orientation_);
    form->addRow(twistLabel_, twist_);
    form->addRow(removeButton_);
    root->addWidget(settingsBox_);

    // --- Bake ---
    bakeBox_ = new QGroupBox(this);
    auto* bakeForm = new QFormLayout(bakeBox_);
    bakeStart_ = new QSpinBox(this);
    bakeStart_->setObjectName("ikBakeStart");
    bakeStart_->setRange(0, 100000);
    bakeEnd_ = new QSpinBox(this);
    bakeEnd_->setObjectName("ikBakeEnd");
    bakeEnd_->setRange(0, 100000);
    bakeEnd_->setValue(30);
    bakeDisable_ = new QCheckBox(this);
    bakeDisable_->setChecked(true);
    bakeButton_ = new QPushButton(this);
    bakeButton_->setObjectName("ikBake");
    auto* range = new QHBoxLayout;
    range->addWidget(bakeStart_);
    range->addWidget(bakeEnd_);
    bakeForm->addRow(range);
    bakeForm->addRow(bakeDisable_);
    bakeForm->addRow(bakeButton_);
    root->addWidget(bakeBox_);

    // --- Verrous de pied ---
    lockBox_ = new QGroupBox(this);
    auto* lockForm = new QFormLayout(lockBox_);
    lockStart_ = new QSpinBox(this);
    lockStart_->setObjectName("ikLockStart");
    lockStart_->setRange(0, 100000);
    lockEnd_ = new QSpinBox(this);
    lockEnd_->setObjectName("ikLockEnd");
    lockEnd_->setRange(0, 100000);
    lockEnd_->setValue(15);
    lockButton_ = new QPushButton(this);
    lockButton_->setObjectName("ikLock");
    lockList_ = new QListWidget(this);
    lockList_->setObjectName("ikLockList");
    lockList_->setMaximumHeight(70);
    unlockButton_ = new QPushButton(this);
    unlockButton_->setObjectName("ikUnlock");
    auto* lockRange = new QHBoxLayout;
    lockRange->addWidget(lockStart_);
    lockRange->addWidget(lockEnd_);
    lockForm->addRow(lockRange);
    lockForm->addRow(lockButton_);
    lockForm->addRow(lockList_);
    lockForm->addRow(unlockButton_);
    root->addWidget(lockBox_);
    root->addStretch(1);

    connect(createButton_, &QPushButton::clicked, this, &IKPanel::createChain);
    connect(lookAtButton_, &QPushButton::clicked, this, &IKPanel::createLookAt);
    connect(humanoidButton_, &QPushButton::clicked, this, &IKPanel::createHumanoid);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { refreshDetails(); });
    connect(toggleButton_, &QPushButton::clicked, this, [this] { document_->toggleIK(currentControl()); });
    connect(weight_, &QSlider::valueChanged, this, [this](int value) {
        if (updating_) return;
        document_->editIK(
            currentControl(), [value](IKControl& c) { c.weight = static_cast<float>(value) / 100.0f; },
            "Edit IK", "weight");
    });
    connect(weight_, &QSlider::sliderReleased, document_, &Document::endContinuousEdit);
    connect(controlSolver_, &QComboBox::activated, this, [this](int index) {
        document_->editIK(currentControl(), [index](IKControl& c) {
            c.chain.solver = index == 0 ? IKSolver::TwoBone : (index == 1 ? IKSolver::FABRIK : IKSolver::CCD);
        });
    });
    connect(orientation_, &QComboBox::activated, this, [this](int index) {
        document_->editIK(currentControl(), [index](IKControl& c) {
            c.chain.effectorOrientation = index == 0   ? EffectorOrientation::Follow
                                          : index == 1 ? EffectorOrientation::KeepWorld
                                                       : EffectorOrientation::MatchTarget;
        });
    });
    connect(twist_, &QDoubleSpinBox::valueChanged, this, [this](double degrees) {
        if (updating_) return;
        document_->editIK(
            currentControl(),
            [degrees](IKControl& c) { c.poleTwist = glm::radians(static_cast<float>(degrees)); }, "Edit IK",
            "twist");
    });
    connect(twist_, &QDoubleSpinBox::editingFinished, document_, &Document::endContinuousEdit);
    connect(removeButton_, &QPushButton::clicked, this, [this] { document_->removeIK(currentControl()); });
    connect(bakeButton_, &QPushButton::clicked, this, [this] {
        const auto result = document_->bakeIK(currentControl(), bakeStart_->value(), bakeEnd_->value(),
                                              bakeDisable_->isChecked());
        if (!result) report(QString::fromStdString(result.error().message));
    });
    connect(lockButton_, &QPushButton::clicked, this, [this] {
        const auto result = document_->addFootLock(currentControl(), lockStart_->value(), lockEnd_->value());
        if (!result) report(QString::fromStdString(result.error().message));
    });
    connect(unlockButton_, &QPushButton::clicked, this,
            [this] { document_->removeFootLock(currentControl(), lockList_->currentRow()); });
    connect(document_, &Document::ikChanged, this, &IKPanel::refresh);
    connect(document_, &Document::characterChanged, this, &IKPanel::refresh);
    connect(document_, &Document::poseChanged, this, &IKPanel::refreshDetails);
    connect(document_, &Document::selectionChanged, this, &IKPanel::refreshDetails);
    retranslate();
    refresh();
}

int IKPanel::currentControl() const { return list_->currentRow(); }

void IKPanel::report(const QString& message) { emit statusMessage(message); }

void IKPanel::createChain() {
    const auto& selection = document_->selection();
    if (selection.empty()) {
        report(tr("Select the bone the chain should reach (hand, foot...) first"));
        return;
    }
    const IKSolver solver = static_cast<IKSolver>(solver_->currentData().toInt());
    const auto result =
        document_->createIK(selection.front(), boneCount_->value(), solver, withPole_->isChecked());
    if (!result) {
        report(QString::fromStdString(result.error().message));
        return;
    }
    list_->setCurrentRow(result.value());
}

void IKPanel::createLookAt() {
    const auto& selection = document_->selection();
    if (selection.empty()) {
        report(tr("Select the bone that should look at a target (head, eye...) first"));
        return;
    }
    const auto result = document_->createLookAt(selection.front());
    if (!result) {
        report(QString::fromStdString(result.error().message));
        return;
    }
    list_->setCurrentRow(result.value());
}

void IKPanel::createHumanoid() {
    const auto result = document_->createHumanoidIK();
    if (!result) {
        report(QString::fromStdString(result.error().message));
        return;
    }
    report(tr("IK controls created: %1").arg(result.value()));
}

void IKPanel::refresh() {
    const int keep = list_->currentRow();
    updating_ = true;
    list_->clear();
    for (const IKControl& c : document_->ikControls()) {
        const QString kind = c.kind == IKKind::LookAt              ? tr("look-at")
                             : c.chain.solver == IKSolver::TwoBone ? tr("two-bone")
                             : c.chain.solver == IKSolver::FABRIK  ? QStringLiteral("FABRIK")
                                                                   : QStringLiteral("CCD");
        list_->addItem(QStringLiteral("%1 (%2)").arg(QString::fromStdString(c.name), kind));
    }
    list_->setCurrentRow(std::min(keep, list_->count() - 1));
    updating_ = false;
    refreshDetails();
}

void IKPanel::refreshDetails() {
    updating_ = true;
    const IKControl* control = controlAt(document_, currentControl());
    const bool has = control != nullptr;
    const bool chain = has && control->kind == IKKind::Chain;
    for (QWidget* w : std::initializer_list<QWidget*>{settingsBox_, bakeBox_, lockBox_}) w->setEnabled(has);
    controlSolver_->setEnabled(chain);
    orientation_->setEnabled(chain);
    twist_->setEnabled(chain && control->poleBone != kNoBone);
    if (has) {
        weight_->setValue(static_cast<int>(std::lround(control->weight * 100.0f)));
        toggleButton_->setText(control->enabled && control->weight > 0.5f ? tr("Switch to FK")
                                                                          : tr("Switch to IK"));
        controlSolver_->setCurrentIndex(control->chain.solver == IKSolver::TwoBone  ? 0
                                        : control->chain.solver == IKSolver::FABRIK ? 1
                                                                                    : 2);
        orientation_->setCurrentIndex(control->chain.effectorOrientation == EffectorOrientation::Follow ? 0
                                      : control->chain.effectorOrientation == EffectorOrientation::KeepWorld
                                          ? 1
                                          : 2);
        twist_->setValue(static_cast<double>(glm::degrees(control->poleTwist)));
        lockList_->clear();
        for (const FootLock& lock : control->locks) {
            lockList_->addItem(tr("Frames %1 - %2")
                                   .arg(std::lround(lock.start * document_->fps()))
                                   .arg(std::lround(lock.end * document_->fps())));
        }
    } else {
        lockList_->clear();
        toggleButton_->setText(tr("Switch to FK"));
    }
    updating_ = false;
}

void IKPanel::retranslate() {
    solver_->setItemText(0, tr("Two-Bone"));
    solver_->setItemText(1, QStringLiteral("FABRIK"));
    solver_->setItemText(2, QStringLiteral("CCD"));
    boneCount_->setToolTip(
        tr("Number of bones in the chain, counted up from the parent of the selected bone"));
    solver_->setToolTip(tr("IK solver"));
    withPole_->setText(tr("Pole"));
    withPole_->setToolTip(tr("Create a pole target that steers the elbow / knee (Two-Bone)"));
    createButton_->setText(tr("Create IK on Selected Bone"));
    createButton_->setToolTip(tr("The selected bone (hand, foot...) becomes the end of the chain"));
    lookAtButton_->setText(tr("Look-At"));
    lookAtButton_->setToolTip(tr("Make the selected bone look at a new target"));
    humanoidButton_->setText(tr("Humanoid Preset"));
    humanoidButton_->setToolTip(tr("Arms, legs, spine and head, found by bone name"));
    settingsBox_->setTitle(tr("Selected Control"));
    weightLabel_->setText(tr("IK weight"));
    solverLabel_->setText(tr("Solver"));
    controlSolver_->setItemText(0, tr("Two-Bone"));
    controlSolver_->setItemText(1, QStringLiteral("FABRIK"));
    controlSolver_->setItemText(2, QStringLiteral("CCD"));
    orientationLabel_->setText(tr("End bone"));
    orientation_->setItemText(0, tr("Follow parent"));
    orientation_->setItemText(1, tr("Keep orientation"));
    orientation_->setItemText(2, tr("Match target"));
    twistLabel_->setText(tr("Pole twist"));
    removeButton_->setText(tr("Remove IK"));
    bakeBox_->setTitle(tr("Bake IK to FK"));
    bakeDisable_->setText(tr("Disable IK afterwards"));
    bakeButton_->setText(tr("Bake"));
    bakeButton_->setToolTip(tr("Convert the IK pose into rotation keys on every frame of the range"));
    lockBox_->setTitle(tr("Foot Lock"));
    lockButton_->setText(tr("Lock Between Frames"));
    lockButton_->setToolTip(tr("Pin the target where it is at the first frame until the last frame"));
    unlockButton_->setText(tr("Remove Lock"));
    refreshDetails();
}

void IKPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace rigger::editor
