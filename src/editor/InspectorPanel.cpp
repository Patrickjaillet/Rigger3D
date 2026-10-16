// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/InspectorPanel.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/math/quat_utils.hpp"
#include "editor/Document.hpp"
#include "editor/Preferences.hpp"
#include "editor/ScrubField.hpp"

namespace rigger::editor {

namespace {

constexpr double kMixedTolerance = 1e-6;

BoneFlags withFlag(BoneFlags flags, BoneFlags bit, bool on) {
    const auto value = static_cast<std::uint32_t>(flags);
    const auto mask = static_cast<std::uint32_t>(bit);
    return static_cast<BoneFlags>(on ? (value | mask) : (value & ~mask));
}

double radiansToDisplay(double radians, AngleUnit unit) {
    return unit == AngleUnit::Degrees ? radians * 180.0 / glm::pi<double>() : radians;
}

double displayToRadians(double value, AngleUnit unit) {
    return unit == AngleUnit::Degrees ? value * glm::pi<double>() / 180.0 : value;
}

glm::vec3 component(const Transform& t, int group) {
    switch (group) {
        case 0:
            return t.t;
        case 1:
            return toEulerXYZ(t.r);  // radians
        default:
            return t.s;
    }
}

}  // namespace

InspectorPanel::InspectorPanel(Document* document, Preferences* preferences, QWidget* parent)
    : QWidget(parent), document_(document), preferences_(preferences) {
    setObjectName("inspectorPanel");
    build();
    retranslate();

    connect(document_, &Document::selectionChanged, this, &InspectorPanel::refresh);
    connect(document_, &Document::characterChanged, this, &InspectorPanel::refresh);
    connect(document_, &Document::hierarchyChanged, this, &InspectorPanel::refresh);
    connect(document_, &Document::modeChanged, this, [this] {
        retranslate();
        refresh();
    });
    connect(document_, &Document::boneChanged, this, [this](BoneId id) {
        if (document_->isSelected(id)) {
            refresh();
        }
    });
    connect(document_, &Document::poseChanged, this, &InspectorPanel::refresh);
    connect(preferences_, &Preferences::changed, this, [this](const QString& key) {
        if (key == QLatin1String("angleUnit")) {
            retranslate();
            refresh();
        }
    });
    refresh();
}

void InspectorPanel::build() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    stack_ = new QStackedWidget(this);
    outer->addWidget(stack_);

    emptyLabel_ = new QLabel(this);
    emptyLabel_->setObjectName("inspectorEmptyLabel");
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setProperty("dim", true);
    stack_->addWidget(emptyLabel_);

    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    selectionLabel_ = new QLabel(page);
    selectionLabel_->setObjectName("inspectorSelectionLabel");
    selectionLabel_->setProperty("dim", true);
    layout->addWidget(selectionLabel_);

    auto* nameRow = new QHBoxLayout;
    nameLabel_ = new QLabel(page);
    nameEdit_ = new QLineEdit(page);
    nameEdit_->setObjectName("inspectorName");
    nameRow->addWidget(nameLabel_);
    nameRow->addWidget(nameEdit_, 1);
    layout->addLayout(nameRow);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &InspectorPanel::editName);

    transformGroup_ = new QGroupBox(page);
    auto* grid = new QGridLayout(transformGroup_);
    static const char* const kGroupNames[3] = {"Translation", "Rotation", "Scale"};
    static const char* const kAxisNames[3] = {"X", "Y", "Z"};
    for (int g = 0; g < 3; ++g) {
        rowLabels_[static_cast<std::size_t>(g)] = new QLabel(transformGroup_);
        grid->addWidget(rowLabels_[static_cast<std::size_t>(g)], g, 0);
        for (int a = 0; a < 3; ++a) {
            auto* field = new ScrubField(transformGroup_);
            field->setObjectName(QStringLiteral("inspector%1%2")
                                     .arg(QLatin1String(kGroupNames[g]), QLatin1String(kAxisNames[a])));
            field->setStep(g == 2 ? 0.005 : (g == 1 ? 0.5 : 0.005));
            fields_[static_cast<std::size_t>(g)][static_cast<std::size_t>(a)] = field;
            grid->addWidget(field, g, a + 1);
            connect(field, &ScrubField::valueEdited, this,
                    [this, g, a](double v) { editComponent(static_cast<Group>(g), a, v); });
            connect(field, &ScrubField::editFinished, document_, &Document::endContinuousEdit);
        }
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    layout->addWidget(transformGroup_);

    auto* props = new QGridLayout;
    lengthLabel_ = new QLabel(page);
    lengthField_ = new ScrubField(page);
    lengthField_->setObjectName("inspectorLength");
    lengthField_->setRange(0.0001, 1000.0);
    lengthField_->setStep(0.005);
    props->addWidget(lengthLabel_, 0, 0);
    props->addWidget(lengthField_, 0, 1);
    connect(lengthField_, &ScrubField::valueEdited, this, &InspectorPanel::editLength);
    connect(lengthField_, &ScrubField::editFinished, document_, &Document::endContinuousEdit);

    colorLabel_ = new QLabel(page);
    colorButton_ = new QPushButton(page);
    colorButton_->setObjectName("inspectorColorButton");
    props->addWidget(colorLabel_, 1, 0);
    props->addWidget(colorButton_, 1, 1);
    connect(colorButton_, &QPushButton::clicked, this, &InspectorPanel::pickColor);
    props->setColumnStretch(1, 1);
    layout->addLayout(props);

    hiddenBox_ = new QCheckBox(page);
    hiddenBox_->setObjectName("inspectorHidden");
    lockedBox_ = new QCheckBox(page);
    lockedBox_->setObjectName("inspectorLocked");
    layout->addWidget(hiddenBox_);
    layout->addWidget(lockedBox_);
    connect(hiddenBox_, &QCheckBox::clicked, this,
            [this] { editFlag(BoneFlags::Hidden, hiddenBox_->checkState() == Qt::Checked); });
    connect(lockedBox_, &QCheckBox::clicked, this,
            [this] { editFlag(BoneFlags::Locked, lockedBox_->checkState() == Qt::Checked); });
    layout->addStretch(1);
    stack_->addWidget(page);
}

void InspectorPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        refresh();
    }
    QWidget::changeEvent(event);
}

void InspectorPanel::retranslate() {
    emptyLabel_->setText(tr("No bone selected.\nSelect a bone in the hierarchy."));
    nameLabel_->setText(tr("Name"));
    const bool bind = document_->mode() == Mode::Rig;
    transformGroup_->setTitle(bind ? tr("Transform (bind pose)") : tr("Transform (current pose)"));
    rowLabels_[0]->setText(tr("Position"));
    rowLabels_[1]->setText(tr("Rotation"));
    rowLabels_[2]->setText(tr("Scale"));
    const bool degrees = preferences_->angleUnit() == AngleUnit::Degrees;
    for (auto& field : fields_[0]) {
        field->setSuffix(tr(" m"));
    }
    for (auto& field : fields_[1]) {
        field->setSuffix(degrees ? QStringLiteral("°") : tr(" rad"));
        field->setStep(degrees ? 0.5 : 0.01);
        field->setDecimals(degrees ? 2 : 4);
    }
    lengthLabel_->setText(tr("Length"));
    lengthField_->setSuffix(tr(" m"));
    colorLabel_->setText(tr("Color"));
    hiddenBox_->setText(tr("Hidden"));
    lockedBox_->setText(tr("Locked"));
}

void InspectorPanel::refresh() {
    const auto& selection = document_->selection();
    const Skeleton* skeleton = document_->skeleton();
    if (selection.empty() || skeleton == nullptr) {
        stack_->setCurrentIndex(0);
        return;
    }
    stack_->setCurrentIndex(1);
    updating_ = true;

    selectionLabel_->setText(selection.size() == 1 ? tr("1 bone selected")
                                                   : tr("%1 bones selected").arg(selection.size()));

    // Nom (édition d'un seul os à la fois : les noms sont uniques).
    if (selection.size() == 1) {
        nameEdit_->setEnabled(true);
        if (!nameEdit_->hasFocus()) {
            nameEdit_->setText(QString::fromStdString(skeleton->bone(selection.front()).name));
        }
        nameEdit_->setPlaceholderText({});
    } else {
        nameEdit_->setEnabled(false);
        nameEdit_->clear();
        nameEdit_->setPlaceholderText(QStringLiteral("—"));
    }

    // Transformation.
    const AngleUnit unit = preferences_->angleUnit();
    bool allLocked = true;
    for (const BoneId id : selection) {
        allLocked = allLocked && hasFlag(skeleton->bone(id).flags, BoneFlags::Locked);
    }
    for (int g = 0; g < 3; ++g) {
        for (int a = 0; a < 3; ++a) {
            ScrubField* field = fields_[static_cast<std::size_t>(g)][static_cast<std::size_t>(a)];
            field->setEnabled(!allLocked);
            if (field->isBusy()) {
                continue;
            }
            double lo = 0.0;
            double hi = 0.0;
            bool first = true;
            for (const BoneId id : selection) {
                double v = component(document_->boneLocal(id), g)[a];
                if (g == Rotation) {
                    v = radiansToDisplay(v, unit);
                }
                lo = first ? v : std::min(lo, v);
                hi = first ? v : std::max(hi, v);
                first = false;
            }
            field->setValue(lo);
            field->setMixed(hi - lo > kMixedTolerance);
        }
    }

    // Longueur.
    if (!lengthField_->isBusy()) {
        double lo = 0.0;
        double hi = 0.0;
        bool first = true;
        for (const BoneId id : selection) {
            const double v = static_cast<double>(skeleton->bone(id).length);
            lo = first ? v : std::min(lo, v);
            hi = first ? v : std::max(hi, v);
            first = false;
        }
        lengthField_->setValue(lo);
        lengthField_->setMixed(hi - lo > kMixedTolerance);
        lengthField_->setEnabled(!allLocked);
    }

    // Couleur.
    bool sameColor = true;
    const glm::vec3 c0 = skeleton->bone(selection.front()).color;
    for (const BoneId id : selection) {
        sameColor =
            sameColor && glm::all(glm::lessThan(glm::abs(skeleton->bone(id).color - c0), glm::vec3(1e-4f)));
    }
    if (sameColor) {
        QPixmap swatch(40, 14);
        swatch.fill(QColor::fromRgbF(static_cast<double>(c0.r), static_cast<double>(c0.g),
                                     static_cast<double>(c0.b)));
        colorButton_->setIcon(QIcon(swatch));
        colorButton_->setIconSize(swatch.size());
        colorButton_->setText({});
    } else {
        colorButton_->setIcon({});
        colorButton_->setText(QStringLiteral("—"));
    }
    colorButton_->setEnabled(!allLocked);

    // Drapeaux (case à 3 états si mixte).
    const auto syncFlag = [&](QCheckBox* box, BoneFlags flag) {
        int on = 0;
        for (const BoneId id : selection) {
            on += hasFlag(skeleton->bone(id).flags, flag) ? 1 : 0;
        }
        box->setTristate(on != 0 && on != static_cast<int>(selection.size()));
        box->setCheckState(
            on == 0 ? Qt::Unchecked
                    : (on == static_cast<int>(selection.size()) ? Qt::Checked : Qt::PartiallyChecked));
    };
    syncFlag(hiddenBox_, BoneFlags::Hidden);
    syncFlag(lockedBox_, BoneFlags::Locked);
    updating_ = false;
}

void InspectorPanel::editComponent(Group group, int axis, double value) {
    if (updating_) {
        return;
    }
    const AngleUnit unit = preferences_->angleUnit();
    std::vector<std::pair<BoneId, Transform>> edits;
    for (const BoneId id : document_->selection()) {
        Transform t = document_->boneLocal(id);
        switch (group) {
            case Translation:
                t.t[axis] = static_cast<float>(value);
                break;
            case Rotation: {
                glm::vec3 euler = toEulerXYZ(t.r);
                euler[axis] = static_cast<float>(displayToRadians(value, unit));
                t.r = fromEulerXYZ(euler);
                break;
            }
            case Scale:
                t.s[axis] = static_cast<float>(value);
                break;
        }
        edits.emplace_back(id, t);
    }
    // Un glissé de curseur = une seule entrée d'historique (clé de fusion par composante).
    document_->setBonesLocal(edits, "inspector:" + std::to_string(group) + ":" + std::to_string(axis));
}

void InspectorPanel::editLength(double value) {
    if (updating_) {
        return;
    }
    document_->setBonesProperties(
        document_->selection(),
        [value](BoneProps& p) { p.length = std::max(static_cast<float>(value), 1e-4f); }, "Edit Bone",
        "inspector:length");
}

void InspectorPanel::editName() {
    if (updating_ || document_->selection().size() != 1) {
        return;
    }
    const BoneId id = document_->selection().front();
    const auto result = document_->renameBone(id, nameEdit_->text());
    if (!result) {
        emit statusMessage(tr("Rename failed: %1").arg(QString::fromStdString(result.error().message)));
    }
    refresh();
    if (document_->skeleton() != nullptr && document_->skeleton()->contains(id)) {
        nameEdit_->setText(QString::fromStdString(document_->skeleton()->bone(id).name));
    }
}

void InspectorPanel::pickColor() {
    const Skeleton* skeleton = document_->skeleton();
    if (skeleton == nullptr || document_->selection().empty()) {
        return;
    }
    const glm::vec3 c = skeleton->bone(document_->selection().front()).color;
    const QColor initial =
        QColor::fromRgbF(static_cast<double>(c.r), static_cast<double>(c.g), static_cast<double>(c.b));
    const QColor chosen = QColorDialog::getColor(initial, this, tr("Bone color"));
    if (!chosen.isValid()) {
        return;
    }
    const glm::vec3 color(static_cast<float>(chosen.redF()), static_cast<float>(chosen.greenF()),
                          static_cast<float>(chosen.blueF()));
    document_->setBonesProperties(
        document_->selection(), [color](BoneProps& p) { p.color = color; }, "Edit Bone");
}

void InspectorPanel::editFlag(BoneFlags flag, bool on) {
    if (updating_ || document_->skeleton() == nullptr) {
        return;
    }
    document_->setBonesProperties(
        document_->selection(), [flag, on](BoneProps& p) { p.flags = withFlag(p.flags, flag, on); },
        "Change Bone Flags", {}, true);
}

}  // namespace rigger::editor
