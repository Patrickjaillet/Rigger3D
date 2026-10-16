// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QAction>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace rigger::editor {

class Preferences;

// Identifiants d'actions : TOUTES les actions de l'application sont déclarées une seule fois dans
// ActionRegistry.cpp (id, texte tr(), icône, raccourci par défaut, contexte).
namespace ActionId {
inline const QString FileNew = QStringLiteral("file.new");
inline const QString FileOpen = QStringLiteral("file.open");
inline const QString FileSave = QStringLiteral("file.save");
inline const QString FileSaveAs = QStringLiteral("file.saveAs");
inline const QString FileExportImage = QStringLiteral("file.exportImage");
inline const QString FileExportSequence = QStringLiteral("file.exportSequence");
inline const QString FileImport = QStringLiteral("file.import");
inline const QString FileExport = QStringLiteral("file.export");
inline const QString FileQuit = QStringLiteral("file.quit");
inline const QString EditUndo = QStringLiteral("edit.undo");
inline const QString EditRedo = QStringLiteral("edit.redo");
inline const QString EditPreferences = QStringLiteral("edit.preferences");
inline const QString ViewResetLayout = QStringLiteral("view.resetLayout");
inline const QString ViewFocus = QStringLiteral("view.focus");
inline const QString ViewResetCamera = QStringLiteral("view.resetCamera");
inline const QString ViewOrthographic = QStringLiteral("view.orthographic");
inline const QString ViewGpuSkinning = QStringLiteral("view.gpuSkinning");
inline const QString ViewInfluence = QStringLiteral("view.influence");
inline const QString ModeRig = QStringLiteral("mode.rig");
inline const QString ModeSkin = QStringLiteral("mode.skin");
inline const QString ModeAnimate = QStringLiteral("mode.animate");
inline const QString ToolSelect = QStringLiteral("tool.select");
inline const QString ToolMove = QStringLiteral("tool.move");
inline const QString ToolRotate = QStringLiteral("tool.rotate");
inline const QString ToolScale = QStringLiteral("tool.scale");
inline const QString AnimPlay = QStringLiteral("anim.play");
inline const QString AnimNextFrame = QStringLiteral("anim.nextFrame");
inline const QString AnimPrevFrame = QStringLiteral("anim.prevFrame");
inline const QString AnimInsertKey = QStringLiteral("anim.insertKey");
inline const QString AnimFirstFrame = QStringLiteral("anim.firstFrame");
inline const QString AnimLastFrame = QStringLiteral("anim.lastFrame");
inline const QString AnimAutoKey = QStringLiteral("anim.autoKey");
inline const QString AnimToggleIK = QStringLiteral("anim.toggleIK");
inline const QString AnimOnionSkin = QStringLiteral("anim.onionSkin");
inline const QString SelectAll = QStringLiteral("select.all");
inline const QString SelectNone = QStringLiteral("select.none");
inline const QString ViewGizmoSpace = QStringLiteral("view.gizmoSpace");
inline const QString RigAddBone = QStringLiteral("rig.addBone");
inline const QString RigExtrude = QStringLiteral("rig.extrude");
inline const QString RigSubdivide = QStringLiteral("rig.subdivide");
inline const QString RigDelete = QStringLiteral("rig.delete");
inline const QString RigDeleteTree = QStringLiteral("rig.deleteTree");
inline const QString PoseReset = QStringLiteral("pose.reset");
inline const QString PoseResetAll = QStringLiteral("pose.resetAll");
inline const QString PoseCopy = QStringLiteral("pose.copy");
inline const QString PosePaste = QStringLiteral("pose.paste");
inline const QString PosePasteMirrored = QStringLiteral("pose.pasteMirrored");
inline const QString PoseSymmetry = QStringLiteral("pose.symmetry");
inline const QString BoneHide = QStringLiteral("bone.hide");
inline const QString BoneShowAll = QStringLiteral("bone.showAll");
inline const QString BoneIsolate = QStringLiteral("bone.isolate");
inline const QString BoneLock = QStringLiteral("bone.lock");
inline const QString BoneUnlockAll = QStringLiteral("bone.unlockAll");
inline const QString HelpQuickStart = QStringLiteral("help.quickStart");
inline const QString HelpAbout = QStringLiteral("help.about");
}  // namespace ActionId

class ActionRegistry : public QObject {
    Q_OBJECT
public:
    struct Entry {
        QString id;
        const char* text;  // source anglaise (QT_TRANSLATE_NOOP), traduite par retranslate()
        const char* statusTip;
        const char* icon;  // nom d'icône SVG ou ""
        QKeySequence defaultShortcut;
        bool viewportScope;  // raccourci actif seulement quand le viewport a le focus
        bool checkable;
        QAction* action = nullptr;
    };

    ActionRegistry(Preferences& preferences, QObject* parent);

    QAction* action(const QString& id) const;
    const std::vector<Entry>& entries() const { return entries_; }

    QKeySequence shortcut(const QString& id) const;
    QKeySequence defaultShortcut(const QString& id) const;
    // Applique et mémorise un raccourci personnalisé (séquence vide = aucun raccourci).
    void setShortcut(const QString& id, const QKeySequence& sequence);
    void resetShortcut(const QString& id);
    // Id de l'action qui utilise déjà `sequence` (hors `id`), sinon chaîne vide.
    QString conflictingAction(const QString& id, const QKeySequence& sequence) const;

    // Rafraîchit textes / info-bulles (changement de langue) et icônes (changement de thème).
    void retranslate();
    void refreshIcons();

    // Texte lisible d'une action, sans mnémonique (« &Import... » -> « Import... »).
    static QString plainText(const QAction* action);

private:
    Entry& entry(const QString& id);
    const Entry& entry(const QString& id) const;
    void applyShortcut(Entry& entry);
    void refreshToolTip(Entry& entry);

    Preferences& preferences_;
    std::vector<Entry> entries_;
};

}  // namespace rigger::editor
