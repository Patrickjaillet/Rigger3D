// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/ActionRegistry.hpp"

#include <QCoreApplication>
#include <cassert>

#include "editor/Preferences.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

namespace {
constexpr const char* kCtx = "ActionRegistry";
}

ActionRegistry::ActionRegistry(Preferences& preferences, QObject* parent)
    : QObject(parent), preferences_(preferences) {
    const auto K = [](QKeySequence::StandardKey k) { return QKeySequence(k); };
    const auto S = [](const char* text) { return QKeySequence(QString::fromLatin1(text)); };

    // clang-format off
    entries_ = {
        // id                         texte (EN)                         info-bulle (EN)                                         icône            raccourci               viewport checkable
        {ActionId::FileNew,           QT_TRANSLATE_NOOP("ActionRegistry", "&New"),               QT_TRANSLATE_NOOP("ActionRegistry", "Start an empty scene"),               "file-new",      K(QKeySequence::New),   false, false},
        {ActionId::FileOpen,          QT_TRANSLATE_NOOP("ActionRegistry", "&Open..."),           QT_TRANSLATE_NOOP("ActionRegistry", "Open a Rigger project (.rig)"), "folder-open",   K(QKeySequence::Open),  false, false},
        {ActionId::FileSave,          QT_TRANSLATE_NOOP("ActionRegistry", "&Save"),              QT_TRANSLATE_NOOP("ActionRegistry", "Save the project"), "save",         K(QKeySequence::Save),  false, false},
        {ActionId::FileSaveAs,        QT_TRANSLATE_NOOP("ActionRegistry", "Save &As..."),        QT_TRANSLATE_NOOP("ActionRegistry", "Save the project under a new name"), "save",         S("Ctrl+Shift+S"),      false, false},
        {ActionId::FileExportImage,   QT_TRANSLATE_NOOP("ActionRegistry", "Export &Image..."),   QT_TRANSLATE_NOOP("ActionRegistry", "Save the 3D viewport as a PNG image"), "export",      QKeySequence(),         false, false},
        {ActionId::FileExportSequence, QT_TRANSLATE_NOOP("ActionRegistry", "Export Image &Sequence..."), QT_TRANSLATE_NOOP("ActionRegistry", "Save a PNG sequence (and optionally an MP4) of the animation"), "export", QKeySequence(), false, false},
        {ActionId::FileImport,        QT_TRANSLATE_NOOP("ActionRegistry", "&Import..."),         QT_TRANSLATE_NOOP("ActionRegistry", "Import a glTF / GLB character"),     "import",        S("Ctrl+I"),            false, false},
        {ActionId::FileExport,        QT_TRANSLATE_NOOP("ActionRegistry", "&Export glTF..."),         QT_TRANSLATE_NOOP("ActionRegistry", "Export the character, its skin and animations to glTF / GLB"), "export",   S("Ctrl+Shift+E"),      false, false},
        {ActionId::FileQuit,          QT_TRANSLATE_NOOP("ActionRegistry", "&Quit"),              QT_TRANSLATE_NOOP("ActionRegistry", "Quit Rigger"),                       "quit",          K(QKeySequence::Quit),  false, false},
        {ActionId::EditUndo,          QT_TRANSLATE_NOOP("ActionRegistry", "&Undo"),              QT_TRANSLATE_NOOP("ActionRegistry", "Undo the last change"), "undo",      K(QKeySequence::Undo),  false, false},
        {ActionId::EditRedo,          QT_TRANSLATE_NOOP("ActionRegistry", "&Redo"),              QT_TRANSLATE_NOOP("ActionRegistry", "Redo the last undone change"), "redo", S("Ctrl+Y"),       false, false},
        {ActionId::EditPreferences,   QT_TRANSLATE_NOOP("ActionRegistry", "&Preferences..."),    QT_TRANSLATE_NOOP("ActionRegistry", "Theme, language, shortcuts and defaults"), "settings",  S("Ctrl+,"),            false, false},
        {ActionId::ViewResetLayout,   QT_TRANSLATE_NOOP("ActionRegistry", "&Reset Layout"),      QT_TRANSLATE_NOOP("ActionRegistry", "Restore the default panel layout"),  "reset-layout",  QKeySequence(),         false, false},
        {ActionId::ViewFocus,         QT_TRANSLATE_NOOP("ActionRegistry", "&Focus Selection"),   QT_TRANSLATE_NOOP("ActionRegistry", "Frame the selection (or the whole character) in the viewport"), "focus", S("F"), true,  false},
        {ActionId::ViewResetCamera,   QT_TRANSLATE_NOOP("ActionRegistry", "Reset &Camera"),      QT_TRANSLATE_NOOP("ActionRegistry", "Return to the default camera"),      "home",          S("Home"),             true,  false},
        {ActionId::ViewOrthographic,  QT_TRANSLATE_NOOP("ActionRegistry", "&Orthographic View"), QT_TRANSLATE_NOOP("ActionRegistry", "Toggle perspective / orthographic projection"), "ortho",  S("5"),                 true,  true},
        {ActionId::ViewGpuSkinning,   QT_TRANSLATE_NOOP("ActionRegistry", "&GPU Skinning"),      QT_TRANSLATE_NOOP("ActionRegistry", "Toggle CPU / GPU skinning"),         "gpu",           S("G"),                 true,  true},
        {ActionId::ViewInfluence,     QT_TRANSLATE_NOOP("ActionRegistry", "Bone &Influence"),    QT_TRANSLATE_NOOP("ActionRegistry", "Show the weight heat map of the selected bone"), "influence", S("B"),               true,  true},
        {ActionId::ModeRig,           QT_TRANSLATE_NOOP("ActionRegistry", "Rig"),                QT_TRANSLATE_NOOP("ActionRegistry", "Create and edit the skeleton"),      "mode-rig",      S("1"),                 false, true},
        {ActionId::ModeSkin,          QT_TRANSLATE_NOOP("ActionRegistry", "Skin"),               QT_TRANSLATE_NOOP("ActionRegistry", "Edit skinning weights"),             "mode-skin",     S("2"),                 false, true},
        {ActionId::ModeAnimate,       QT_TRANSLATE_NOOP("ActionRegistry", "Animate"),            QT_TRANSLATE_NOOP("ActionRegistry", "Pose the character and animate"),    "mode-animate",  S("3"),                 false, true},
        {ActionId::ToolSelect,        QT_TRANSLATE_NOOP("ActionRegistry", "Select"),             QT_TRANSLATE_NOOP("ActionRegistry", "Selection tool"),                    "tool-select",   S("Q"),                 false, true},
        {ActionId::ToolMove,          QT_TRANSLATE_NOOP("ActionRegistry", "Move"),               QT_TRANSLATE_NOOP("ActionRegistry", "Translation tool"),                  "tool-move",     S("W"),                 false, true},
        {ActionId::ToolRotate,        QT_TRANSLATE_NOOP("ActionRegistry", "Rotate"),             QT_TRANSLATE_NOOP("ActionRegistry", "Rotation tool"),                     "tool-rotate",   S("E"),                 false, true},
        {ActionId::ToolScale,         QT_TRANSLATE_NOOP("ActionRegistry", "Scale"),              QT_TRANSLATE_NOOP("ActionRegistry", "Scale tool"),                        "tool-scale",    S("R"),                 false, true},
        {ActionId::AnimPlay,          QT_TRANSLATE_NOOP("ActionRegistry", "&Play / Pause"),      QT_TRANSLATE_NOOP("ActionRegistry", "Play or pause the animation"),       "play",          S("Space"),             true,  false},
        {ActionId::AnimNextFrame,     QT_TRANSLATE_NOOP("ActionRegistry", "&Next Frame"),        QT_TRANSLATE_NOOP("ActionRegistry", "Go to the next frame"),              "step-forward",  S("Right"),             true,  false},
        {ActionId::AnimPrevFrame,     QT_TRANSLATE_NOOP("ActionRegistry", "Pre&vious Frame"),    QT_TRANSLATE_NOOP("ActionRegistry", "Go to the previous frame"),          "step-back",     S("Left"),              true,  false},
        {ActionId::AnimInsertKey,     QT_TRANSLATE_NOOP("ActionRegistry", "Insert &Key"),        QT_TRANSLATE_NOOP("ActionRegistry", "Insert a keyframe for the selected bones at the current frame"), "keyframe",    S("K"),                 false, false},
        {ActionId::AnimFirstFrame,    QT_TRANSLATE_NOOP("ActionRegistry", "&First Frame"),       QT_TRANSLATE_NOOP("ActionRegistry", "Go to the first frame"),             "skip-start",    S("Shift+Left"),        false, false},
        {ActionId::AnimLastFrame,     QT_TRANSLATE_NOOP("ActionRegistry", "&Last Frame"),        QT_TRANSLATE_NOOP("ActionRegistry", "Go to the last frame"),              "skip-end",      S("Shift+Right"),       false, false},
        {ActionId::AnimAutoKey,       QT_TRANSLATE_NOOP("ActionRegistry", "&Auto Key"),          QT_TRANSLATE_NOOP("ActionRegistry", "Automatically key every pose change in Animate mode"), "record", S("Ctrl+K"),   false, true},
        {ActionId::AnimToggleIK,      QT_TRANSLATE_NOOP("ActionRegistry", "Toggle &IK / FK"),    QT_TRANSLATE_NOOP("ActionRegistry", "Blend the IK control of the selected bone between IK and FK"), "isolate", S("I"), false, false},
        {ActionId::AnimOnionSkin,     QT_TRANSLATE_NOOP("ActionRegistry", "&Onion Skin"),        QT_TRANSLATE_NOOP("ActionRegistry", "Show ghost poses of the neighbouring frames"), "onion",   S("Shift+O"),        false, true},
        {ActionId::SelectAll,         QT_TRANSLATE_NOOP("ActionRegistry", "Select &All"),        QT_TRANSLATE_NOOP("ActionRegistry", "Select every selectable bone"),       "select-all",    QKeySequence(QStringLiteral("A")), true,  false},
        {ActionId::SelectNone,        QT_TRANSLATE_NOOP("ActionRegistry", "Select &None"),       QT_TRANSLATE_NOOP("ActionRegistry", "Clear the selection"),                "select-none",   QKeySequence(QStringLiteral("Alt+A")), true, false},
        {ActionId::ViewGizmoSpace,    QT_TRANSLATE_NOOP("ActionRegistry", "Gizmo &Space"),       QT_TRANSLATE_NOOP("ActionRegistry", "Cycle the gizmo space: Local, Global, Parent"), "space",    QKeySequence(QStringLiteral("X")), true,  false},
        {ActionId::RigAddBone,        QT_TRANSLATE_NOOP("ActionRegistry", "Add &Bone"),          QT_TRANSLATE_NOOP("ActionRegistry", "Add a child bone at the tip of the selected bone (Rig mode)"), "add-bone", QKeySequence(QStringLiteral("Shift+A")), true,  false},
        {ActionId::RigExtrude,        QT_TRANSLATE_NOOP("ActionRegistry", "&Extrude Bone"),      QT_TRANSLATE_NOOP("ActionRegistry", "Add a child bone at the tip of the selected bone and start moving it (Rig mode)"), "extrude", QKeySequence(QStringLiteral("Ctrl+E")), true,  false},
        {ActionId::RigSubdivide,      QT_TRANSLATE_NOOP("ActionRegistry", "S&ubdivide Bone"),    QT_TRANSLATE_NOOP("ActionRegistry", "Split the selected bone in two (Rig mode)"), "subdivide", QKeySequence(QStringLiteral("Ctrl+D")), true,  false},
        {ActionId::RigDelete,         QT_TRANSLATE_NOOP("ActionRegistry", "&Delete Bone"),       QT_TRANSLATE_NOOP("ActionRegistry", "Delete the selected bones, children are kept (Rig mode)"), "clear", QKeySequence(QStringLiteral("Delete")), true,  false},
        {ActionId::RigDeleteTree,     QT_TRANSLATE_NOOP("ActionRegistry", "Delete Bone and &Children"), QT_TRANSLATE_NOOP("ActionRegistry", "Delete the selected bones and all their descendants (Rig mode)"), "", QKeySequence(QStringLiteral("Shift+Delete")), true,  false},
        {ActionId::PoseReset,         QT_TRANSLATE_NOOP("ActionRegistry", "&Reset Pose (Selection)"), QT_TRANSLATE_NOOP("ActionRegistry", "Return the selected bones to their bind pose"), "reset-pose", QKeySequence(QStringLiteral("Alt+R")), false, false},
        {ActionId::PoseResetAll,      QT_TRANSLATE_NOOP("ActionRegistry", "Reset Pose (&All)"),  QT_TRANSLATE_NOOP("ActionRegistry", "Return every bone to its bind pose"),   "",              QKeySequence(QStringLiteral("Ctrl+Alt+R")), false, false},
        {ActionId::PoseCopy,          QT_TRANSLATE_NOOP("ActionRegistry", "&Copy Pose"),         QT_TRANSLATE_NOOP("ActionRegistry", "Copy the pose of the selected bones (or of all bones)"), "copy", QKeySequence(QStringLiteral("Ctrl+C")), false, false},
        {ActionId::PosePaste,         QT_TRANSLATE_NOOP("ActionRegistry", "&Paste Pose"),        QT_TRANSLATE_NOOP("ActionRegistry", "Paste the copied pose onto the bones with the same names"), "paste", QKeySequence(QStringLiteral("Ctrl+V")), false, false},
        {ActionId::PosePasteMirrored, QT_TRANSLATE_NOOP("ActionRegistry", "Paste Pose &Mirrored"), QT_TRANSLATE_NOOP("ActionRegistry", "Paste the copied pose mirrored across X (left <-> right)"), "mirror", QKeySequence(QStringLiteral("Ctrl+Shift+V")), false, false},
        {ActionId::PoseSymmetry,      QT_TRANSLATE_NOOP("ActionRegistry", "&Mirror X Editing"),  QT_TRANSLATE_NOOP("ActionRegistry", "Apply edits to the opposite side too (.L/.R or _L/_R bones)"), "mirror", QKeySequence(QStringLiteral("Shift+M")), false, true},
        {ActionId::BoneHide,          QT_TRANSLATE_NOOP("ActionRegistry", "&Hide Selected"),     QT_TRANSLATE_NOOP("ActionRegistry", "Hide the selected bones"),            "eye-off",       QKeySequence(QStringLiteral("H")), true,  false},
        {ActionId::BoneShowAll,       QT_TRANSLATE_NOOP("ActionRegistry", "&Show All"),          QT_TRANSLATE_NOOP("ActionRegistry", "Show all hidden bones and leave isolation"), "eye",  QKeySequence(QStringLiteral("Alt+H")), true,  false},
        {ActionId::BoneIsolate,       QT_TRANSLATE_NOOP("ActionRegistry", "&Isolate Selection"), QT_TRANSLATE_NOOP("ActionRegistry", "Show only the selected bones"),         "isolate",       QKeySequence(QStringLiteral("/")), true,  true},
        {ActionId::BoneLock,          QT_TRANSLATE_NOOP("ActionRegistry", "&Lock Selected"),     QT_TRANSLATE_NOOP("ActionRegistry", "Lock the selected bones (not editable, not pickable)"), "lock", QKeySequence(QStringLiteral("Ctrl+L")), false, false},
        {ActionId::BoneUnlockAll,     QT_TRANSLATE_NOOP("ActionRegistry", "&Unlock All"),        QT_TRANSLATE_NOOP("ActionRegistry", "Unlock every bone"),                  "unlock",        QKeySequence(QStringLiteral("Ctrl+Alt+L")), false, false},
        {ActionId::HelpQuickStart,    QT_TRANSLATE_NOOP("ActionRegistry", "&Quick Start"),       QT_TRANSLATE_NOOP("ActionRegistry", "Short tutorial"),                    "tutorial",      K(QKeySequence::HelpContents), false, false},
        {ActionId::HelpAbout,         QT_TRANSLATE_NOOP("ActionRegistry", "&About Rigger"),      QT_TRANSLATE_NOOP("ActionRegistry", "About this application"),            "info",          QKeySequence(),         false, false},
    };
    // clang-format on

    for (Entry& e : entries_) {
        e.action = new QAction(this);
        e.action->setObjectName(QStringLiteral("action_") + e.id);
        e.action->setCheckable(e.checkable);
        e.action->setShortcutContext(e.viewportScope ? Qt::WidgetWithChildrenShortcut : Qt::WindowShortcut);
        e.action->setIconVisibleInMenu(true);
        applyShortcut(e);
    }
    // Fonctions pas encore implémentées (phases suivantes).
    for (const QString& id : {ActionId::FileOpen, ActionId::FileSave, ActionId::FileExport}) {
        action(id)->setEnabled(false);
    }
    retranslate();
    refreshIcons();
}

ActionRegistry::Entry& ActionRegistry::entry(const QString& id) {
    for (Entry& e : entries_) {
        if (e.id == id) {
            return e;
        }
    }
    assert(false && "unknown action id");
    return entries_.front();
}

const ActionRegistry::Entry& ActionRegistry::entry(const QString& id) const {
    return const_cast<ActionRegistry*>(this)->entry(id);
}

QAction* ActionRegistry::action(const QString& id) const { return entry(id).action; }

QKeySequence ActionRegistry::defaultShortcut(const QString& id) const { return entry(id).defaultShortcut; }

QKeySequence ActionRegistry::shortcut(const QString& id) const { return entry(id).action->shortcut(); }

void ActionRegistry::applyShortcut(Entry& e) {
    const QKeySequence seq =
        preferences_.hasCustomShortcut(e.id) ? preferences_.customShortcut(e.id) : e.defaultShortcut;
    e.action->setShortcut(seq);
}

void ActionRegistry::setShortcut(const QString& id, const QKeySequence& sequence) {
    Entry& e = entry(id);
    if (sequence == e.defaultShortcut) {
        preferences_.clearCustomShortcut(id);
    } else {
        preferences_.setCustomShortcut(id, sequence);
    }
    e.action->setShortcut(sequence);
    refreshToolTip(e);
}

void ActionRegistry::resetShortcut(const QString& id) {
    Entry& e = entry(id);
    preferences_.clearCustomShortcut(id);
    e.action->setShortcut(e.defaultShortcut);
    refreshToolTip(e);
}

QString ActionRegistry::conflictingAction(const QString& id, const QKeySequence& sequence) const {
    if (sequence.isEmpty()) {
        return {};
    }
    for (const Entry& e : entries_) {
        if (e.id != id && e.action->shortcut() == sequence) {
            return e.id;
        }
    }
    return {};
}

QString ActionRegistry::plainText(const QAction* action) {
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text;
}

void ActionRegistry::refreshToolTip(Entry& e) {
    QString tip = QCoreApplication::translate(kCtx, e.statusTip);
    const QString shortcut = e.action->shortcut().toString(QKeySequence::NativeText);
    const QString title = plainText(e.action);
    e.action->setToolTip(shortcut.isEmpty() ? QStringLiteral("%1 - %2").arg(title, tip)
                                            : QStringLiteral("%1 (%2) - %3").arg(title, shortcut, tip));
}

void ActionRegistry::retranslate() {
    for (Entry& e : entries_) {
        e.action->setText(QCoreApplication::translate(kCtx, e.text));
        e.action->setStatusTip(QCoreApplication::translate(kCtx, e.statusTip));
        refreshToolTip(e);
    }
}

void ActionRegistry::refreshIcons() {
    for (Entry& e : entries_) {
        if (e.icon[0] != '\0') {
            e.action->setIcon(Icons::get(QString::fromLatin1(e.icon)));
        }
    }
}

}  // namespace rigger::editor
