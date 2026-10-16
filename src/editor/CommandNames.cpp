// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/CommandNames.hpp"

#include <QCoreApplication>

namespace rigger::editor {

namespace {
// Références pour lupdate : toutes les clés de nom de commande utilisées par Document.
[[maybe_unused]] const char* const kCommandNames[] = {
    QT_TRANSLATE_NOOP("Command", "Transform Bones"),
    QT_TRANSLATE_NOOP("Command", "Edit Bone"),
    QT_TRANSLATE_NOOP("Command", "Rename Bone"),
    QT_TRANSLATE_NOOP("Command", "Change Bone Flags"),
    QT_TRANSLATE_NOOP("Command", "Add Bone"),
    QT_TRANSLATE_NOOP("Command", "Delete Bone"),
    QT_TRANSLATE_NOOP("Command", "Subdivide Bone"),
    QT_TRANSLATE_NOOP("Command", "Reparent Bone"),
    QT_TRANSLATE_NOOP("Command", "Reset Pose"),
    QT_TRANSLATE_NOOP("Command", "Paste Pose"),
    QT_TRANSLATE_NOOP("Command", "Insert Keys"),
    QT_TRANSLATE_NOOP("Command", "Delete Keys"),
    QT_TRANSLATE_NOOP("Command", "Move Keys"),
    QT_TRANSLATE_NOOP("Command", "Duplicate Keys"),
    QT_TRANSLATE_NOOP("Command", "Scale Keys"),
    QT_TRANSLATE_NOOP("Command", "Paste Keys"),
    QT_TRANSLATE_NOOP("Command", "Set Interpolation"),
    QT_TRANSLATE_NOOP("Command", "Edit Key"),
    QT_TRANSLATE_NOOP("Command", "New Clip"),
    QT_TRANSLATE_NOOP("Command", "Duplicate Clip"),
    QT_TRANSLATE_NOOP("Command", "Delete Clip"),
    QT_TRANSLATE_NOOP("Command", "Rename Clip"),
    QT_TRANSLATE_NOOP("Command", "Edit Clip"),
    QT_TRANSLATE_NOOP("Command", "Save Pose"),
    QT_TRANSLATE_NOOP("Command", "Delete Pose"),
    QT_TRANSLATE_NOOP("Command", "Edit Mix"),
    QT_TRANSLATE_NOOP("Command", "Crossfade"),
    QT_TRANSLATE_NOOP("Command", "Retarget Clip"),
    QT_TRANSLATE_NOOP("Command", "Edit Deform"),
    QT_TRANSLATE_NOOP("Command", "Create Twist Bones"),
    QT_TRANSLATE_NOOP("Command", "Add Constraint"),
    QT_TRANSLATE_NOOP("Command", "Add Collider"),
    QT_TRANSLATE_NOOP("Command", "Create Spring Chain"),
    QT_TRANSLATE_NOOP("Command", "Bake Springs"),
    QT_TRANSLATE_NOOP("Command", "Edit Corrective Shape"),
    QT_TRANSLATE_NOOP("Command", "Auto-Rig"),
    QT_TRANSLATE_NOOP("Command", "Paint Weights"),
    QT_TRANSLATE_NOOP("Command", "Set Weights"),
    QT_TRANSLATE_NOOP("Command", "Auto Skin"),
    QT_TRANSLATE_NOOP("Command", "Normalize Weights"),
    QT_TRANSLATE_NOOP("Command", "Limit Influences"),
    QT_TRANSLATE_NOOP("Command", "Prune Weights"),
    QT_TRANSLATE_NOOP("Command", "Copy Weights"),
    QT_TRANSLATE_NOOP("Command", "Create IK"),
    QT_TRANSLATE_NOOP("Command", "Create Look-At"),
    QT_TRANSLATE_NOOP("Command", "Edit IK"),
    QT_TRANSLATE_NOOP("Command", "Remove IK"),
    QT_TRANSLATE_NOOP("Command", "Toggle IK"),
    QT_TRANSLATE_NOOP("Command", "Bake IK"),
    QT_TRANSLATE_NOOP("Command", "Add Foot Lock"),
    QT_TRANSLATE_NOOP("Command", "Remove Foot Lock"),
    QT_TRANSLATE_NOOP("Command", "Humanoid IK"),
    QT_TRANSLATE_NOOP("Command", "Hide Bones"),
    QT_TRANSLATE_NOOP("Command", "Show All Bones"),
    QT_TRANSLATE_NOOP("Command", "Lock Bones"),
    QT_TRANSLATE_NOOP("Command", "Unlock All Bones"),
};
}  // namespace

QString commandDisplayName(const std::string& key) {
    return QCoreApplication::translate("Command", key.c_str());
}

QString commandText(const rigger::Command& command) {
    QString text = commandDisplayName(command.name());
    const std::string detail = command.detail();
    if (!detail.empty()) {
        text += QStringLiteral(" (%1)").arg(QString::fromStdString(detail));
    }
    return text;
}

}  // namespace rigger::editor
