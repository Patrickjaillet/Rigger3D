// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <functional>
#include <memory>

#include "core/character/character.hpp"
#include "core/result.hpp"
#include "io/gltf_export.hpp"

class QActionGroup;
class QLabel;
class QMenu;
class QMimeData;
class QProgressBar;
class QProgressDialog;
class QStackedWidget;
class QToolBar;

namespace ads {
class CDockManager;
class CDockWidget;
}  // namespace ads

namespace rigger::editor {

class ActionRegistry;
class BackgroundTask;
class CharacterPanel;
class ConsolePanel;
class Document;
class HierarchyPanel;
class HistoryPanel;
class TimelinePanel;
class PoseLibraryPanel;
class GraphPanel;
class IKPanel;
class SkinPanel;
class MixPanel;
class DeformPanel;
class StatsPanel;
class InspectorPanel;
class Preferences;
class PreferencesDialog;
struct ImageSequenceOptions;
class ViewportWidget;
class TutorialGuide;
class WelcomeWidget;

// Fenêtre principale : écran d'accueil / espace de travail (panneaux ancrables ADS), menus et barres d'outils
// issus de l'ActionRegistry, barre de statut (mode, outil, frame, FPS, statistiques, progression).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    Document* document() const { return document_; }
    Preferences* preferences() const { return preferences_; }
    ActionRegistry* actions() const { return actions_; }
    ads::CDockManager* dockManager() const { return dockManager_; }
    HierarchyPanel* hierarchyPanel() const { return hierarchy_; }
    InspectorPanel* inspectorPanel() const { return inspector_; }
    CharacterPanel* characterPanel() const { return characterPanel_; }
    ConsolePanel* consolePanel() const { return console_; }
    HistoryPanel* historyPanel() const { return history_; }
    TimelinePanel* timelinePanel() const { return timeline_; }
    PoseLibraryPanel* poseLibraryPanel() const { return poses_; }
    GraphPanel* graphPanel() const { return graph_; }
    IKPanel* ikPanel() const { return ik_; }
    SkinPanel* skinPanel() const { return skin_; }
    MixPanel* mixPanel() const { return mix_; }
    DeformPanel* deformPanel() const { return deformPanel_; }
    StatsPanel* statsPanel() const { return stats_; }
    WelcomeWidget* welcomeWidget() const { return welcome_; }
    QWidget* viewport() const { return viewport_; }
    QToolBar* modeToolBar() const { return modeBar_; }
    ads::CDockWidget* dock(const QString& objectName) const;
    bool isWorkspaceVisible() const;

    // Import synchrone (CLI, tests) : affiche le personnage sans boîte de dialogue ; l'erreur est renvoyée.
    Result<void> importFile(const QString& path);
    // Import asynchrone (menu, glisser-déposer, récents) : thread de travail, barre de progression annulable,
    // boîte de dialogue en cas d'erreur. importFinished() est émis à la fin (succès, erreur ou annulation).
    void importFileAsync(const QString& path);
    bool isImporting() const { return importTask_ != nullptr; }
    void cancelImport();  // sans effet si aucun import n'est en cours
    // Importe le premier fichier supporté d'un glisser-déposer ; false si aucun ne l'est.
    bool importDropped(const QMimeData& mime);

    // --- Projets, sauvegarde, export (Phase 12) -------------------------------------------------
    enum class UnsavedChoice { Save, Discard, Cancel };
    // Remplace la boîte « enregistrer les modifications ? » (tests) ; sans argument : boîte standard.
    void setUnsavedPrompt(std::function<UnsavedChoice()> handler) { unsavedPrompt_ = std::move(handler); }
    bool confirmDiscard();  // faux si l'utilisateur annule (ou si l'enregistrement échoue)
    const QString& projectPath() const { return projectPath_; }
    Result<void> saveProjectTo(const QString& path);
    Result<void> openProject(const QString& path);
    Result<void> exportGltfTo(const QString& path, const io::GltfExportOptions& options);
    // PNG du viewport (nécessite le rendu OpenGL) ; retourne le nombre d'images écrites ou une erreur.
    Result<int> exportImage(const QString& path, bool transparent, bool hideOverlays);
    Result<int> exportImageSequence(const QString& directory, const QString& baseName,
                                    const ImageSequenceOptions& options);
    // Autosave : écrit le projet courant (s'il est modifié) dans le dossier temporaire ; récupération au
    // lancement.
    static QString autosaveDirectory();
    void autosaveNow();
    bool hasRecoverableAutosave() const;
    Result<void> recoverAutosave();
    void discardAutosave();
    TutorialGuide* tutorial() const { return tutorial_; }
    void startTutorial();  // visite guidée en info-bulles (n'écrit rien, n'envoie rien)
    // Charge le projet d'exemple « Rigby » (CC0) ; jamais écrasé par Enregistrer.
    void openExample(bool withTutorial = false);
    // Demande à l'utilisateur (appelé par main() au lancement).
    void offerRecovery();
    // Image du viewport 3D (nulle si le rendu OpenGL est indisponible, p. ex. plateforme offscreen).
    QImage grabViewport();

    std::shared_ptr<const Character> character() const;

    void resetLayout();
    void saveLayout();
    // Message dans la barre de statut (usage externe : main(), tests).
    void postMessage(const QString& message) { postStatus(message, 15000); }
    // Erreur visible : journal + barre de statut + boîte de dialogue non bloquante (jamais d'échec
    // silencieux).
    void reportError(const QString& title, const QString& message);
    int reportedErrors() const { return reportedErrors_; }

signals:
    void importFinished(bool ok, const QString& message);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void showAbout();
    void showQuickStart();
    void showPreferences();
    void importDialog();
    void newScene();
    void openRecentPath(const QString& path);
    void openDialog();
    void saveProject();
    void saveProjectAs();
    void exportGltfDialog();
    void exportImageDialog();
    void exportSequenceDialog();

private:
    void buildWorkspace();
    void buildActions();
    void buildMenus();
    void buildToolBars();
    void buildStatusBar();
    void connectDocument();
    void applyPreferenceChange(const QString& key);
    void rebuildRecentMenu();
    void applyDockTheme();
    void updateActionStates();
    void updateUndoActions();
    void retranslate();
    void refreshStatus();
    void refreshTitle();
    void showWorkspace();
    void finishImport(const std::shared_ptr<Result<Character>>& result, const QString& path);
    void postStatus(const QString& message, int timeoutMs = 6000);
    ads::CDockWidget* addPanel(const QString& objectName, QWidget* widget);

    Preferences* preferences_ = nullptr;
    Document* document_ = nullptr;
    ActionRegistry* actions_ = nullptr;
    bool constructed_ =
        false;  // faux pendant le constructeur : un LanguageChange précoce ne doit rien toucher

    QStackedWidget* stack_ = nullptr;
    WelcomeWidget* welcome_ = nullptr;
    TutorialGuide* tutorial_ = nullptr;
    QWidget* workspace_ = nullptr;
    ads::CDockManager* dockManager_ = nullptr;
    QByteArray defaultLayout_;
    QString adsBaseStyleSheet_;  // feuille de style par défaut d'ADS, à laquelle s'ajoute celle du thème
    QWidget* viewport_ = nullptr;
    ViewportWidget* glViewport_ = nullptr;
    HierarchyPanel* hierarchy_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    CharacterPanel* characterPanel_ = nullptr;
    ConsolePanel* console_ = nullptr;
    HistoryPanel* history_ = nullptr;
    TimelinePanel* timeline_ = nullptr;
    PoseLibraryPanel* poses_ = nullptr;
    GraphPanel* graph_ = nullptr;
    IKPanel* ik_ = nullptr;
    SkinPanel* skin_ = nullptr;
    MixPanel* mix_ = nullptr;
    DeformPanel* deformPanel_ = nullptr;
    StatsPanel* stats_ = nullptr;

    QMenu* fileMenu_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QMenu* editMenu_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    QMenu* panelsMenu_ = nullptr;
    QMenu* selectMenu_ = nullptr;
    QMenu* boneMenu_ = nullptr;
    QMenu* poseMenu_ = nullptr;
    QMenu* visibilityMenu_ = nullptr;
    QMenu* helpMenu_ = nullptr;
    QToolBar* modeBar_ = nullptr;
    QActionGroup* modeGroup_ = nullptr;
    QActionGroup* toolGroup_ = nullptr;

    QLabel* modeLabel_ = nullptr;
    QLabel* toolLabel_ = nullptr;
    QLabel* frameLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;

    int reportedErrors_ = 0;
    std::function<UnsavedChoice()> unsavedPrompt_;
    QString projectPath_;
    QTimer* autosaveTimer_ = nullptr;
    BackgroundTask* importTask_ = nullptr;
    QProgressDialog* progressDialog_ = nullptr;
    PreferencesDialog* preferencesDialog_ = nullptr;
};

}  // namespace rigger::editor
