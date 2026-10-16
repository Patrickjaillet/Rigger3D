// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/MainWindow.hpp"

// clang-format off
// glad doit être inclus AVANT tout en-tête OpenGL de Qt : ne pas laisser clang-format réordonner.
#include "editor/ViewportWidget.hpp"
// clang-format on

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <spdlog/spdlog.h>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>

#include "core/example/example.hpp"
#include "core/version.hpp"
#include "editor/AboutDialog.hpp"
#include "editor/ActionRegistry.hpp"
#include "editor/BackgroundTask.hpp"
#include "editor/CharacterPanel.hpp"
#include "editor/ConsolePanel.hpp"
#include "editor/DeformPanel.hpp"
#include "editor/Document.hpp"
#include "editor/GraphPanel.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/HistoryPanel.hpp"
#include "editor/IKPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/MixPanel.hpp"
#include "editor/PoseLibraryPanel.hpp"
#include "editor/Preferences.hpp"
#include "editor/PreferencesDialog.hpp"
#include "editor/SkinPanel.hpp"
#include "editor/StatsPanel.hpp"
#include "editor/Theme.hpp"
#include "editor/TimelinePanel.hpp"
#include "editor/Translations.hpp"
#include "editor/TutorialGuide.hpp"
#include "editor/WelcomeWidget.hpp"
#include "io/formats.hpp"
#include "io/gltf_import.hpp"
#include "render/gl_requirements.hpp"

namespace rigger::editor {

namespace {

constexpr int kLayoutVersion = 7;  // 2 : Historique ; 3 : Timeline, Courbes, Poses ; 4 : IK ; 5 : Skin ; 6 :
                                   // Mélange ; 7 : Déformation ; 8 : Statistiques

void configureDefaultOpenGLFormat() {
    const auto defaultFormat = QSurfaceFormat::defaultFormat();
    if (defaultFormat.majorVersion() != 0 || defaultFormat.minorVersion() != 0) {
        return;
    }

    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    format.setDepthBufferSize(24);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);
}

bool isSupportedImportFile(const QString& path) {
    const QString suffix = QStringLiteral(".") + QFileInfo(path).suffix().toLower();
    const auto extensions = rigger::io::supportedImportExtensions();
    return std::any_of(extensions.begin(), extensions.end(), [&](std::string_view ext) {
        return suffix == QString::fromUtf8(ext.data(), static_cast<qsizetype>(ext.size()));
    });
}

bool shouldUseOpenGLViewport() {
    const QString platform = QGuiApplication::platformName();
    return platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal");
}

QString stageText(const QString& stage) {
    if (stage == QLatin1String("Reading file")) {
        return QCoreApplication::translate("Import", "Reading file...");
    }
    if (stage == QLatin1String("Building skeleton")) {
        return QCoreApplication::translate("Import", "Building the skeleton...");
    }
    if (stage == QLatin1String("Building meshes")) {
        return QCoreApplication::translate("Import", "Building the meshes...");
    }
    if (stage == QLatin1String("Reading animations")) {
        return QCoreApplication::translate("Import", "Reading the animations...");
    }
    if (stage == QLatin1String("Validating")) {
        return QCoreApplication::translate("Import", "Validating...");
    }
    return stage;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    initEditorResources();
    configureDefaultOpenGLFormat();

    preferences_ = new Preferences(this);
    applyLanguage(preferences_->language());
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        Theme::setFontSize(preferences_->fontSize());
        Theme::apply(*app, preferences_->theme());
    }
    document_ = new Document(this);
    document_->setDefaultFps(preferences_->defaultFps());
    actions_ = new ActionRegistry(*preferences_, this);

    setObjectName("MainWindow");
    resize(1280, 800);
    setAcceptDrops(true);

    stack_ = new QStackedWidget(this);
    welcome_ = new WelcomeWidget(preferences_, stack_);
    workspace_ = new QWidget(stack_);
    stack_->addWidget(welcome_);
    stack_->addWidget(workspace_);
    setCentralWidget(stack_);

    buildWorkspace();
    buildActions();
    buildMenus();
    buildToolBars();
    buildStatusBar();
    connectDocument();

    connect(preferences_, &Preferences::changed, this, &MainWindow::applyPreferenceChange);
    connect(welcome_, &WelcomeWidget::newRequested, this, &MainWindow::newScene);
    connect(welcome_, &WelcomeWidget::importRequested, this, &MainWindow::importDialog);
    connect(welcome_, &WelcomeWidget::openRecentRequested, this, &MainWindow::openRecentPath);
    connect(welcome_, &WelcomeWidget::exampleRequested, this, [this] { openExample(true); });
    connect(welcome_, &WelcomeWidget::tutorialRequested, this, &MainWindow::showQuickStart);

    // Disposition sauvegardée de la session précédente (sinon disposition par défaut).
    const QByteArray geometry = preferences_->windowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray layout = preferences_->layoutState();
    if (!layout.isEmpty() && !dockManager_->restoreState(layout, kLayoutVersion)) {
        spdlog::warn("saved panel layout could not be restored; using the default layout");
        resetLayout();
    }

    stack_->setCurrentWidget(welcome_);
    // Autosave toutes les 2 minutes (seulement si le projet est modifié).
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(preferences_->autosaveMinutes() * 60 * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosaveNow);
    if (preferences_->autosaveEnabled()) {
        autosaveTimer_->start();
    }
    constructed_ = true;
    retranslate();
    refreshStatus();
    postStatus(tr("Ready"));
}

MainWindow::~MainWindow() {
    // Le travail en cours est annulé puis attendu (BackgroundTask::~BackgroundTask).
    delete importTask_;
    importTask_ = nullptr;
}

// --- Construction ---------------------------------------------------------------------------

ads::CDockWidget* MainWindow::addPanel(const QString& objectName, QWidget* widget) {
    auto* dock = new ads::CDockWidget(dockManager_, objectName);  // titre remplacé par retranslate()
    dock->setObjectName(objectName);
    dock->setWidget(widget);
    return dock;
}

void MainWindow::buildWorkspace() {
    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig);
    auto* layout = new QVBoxLayout(workspace_);
    layout->setContentsMargins(0, 0, 0, 0);
    dockManager_ = new ads::CDockManager(workspace_);
    dockManager_->setObjectName("dockManager");
    adsBaseStyleSheet_ = dockManager_->styleSheet();
    applyDockTheme();
    layout->addWidget(dockManager_);

    // Viewport : widget OpenGL, ou simple zone grisée sans GPU (plateforme offscreen des tests).
    if (shouldUseOpenGLViewport()) {
        glViewport_ = new ViewportWidget(workspace_);
        glViewport_->setDocument(document_);
        viewport_ = glViewport_;
    } else {
        auto* fallback = new QWidget(workspace_);
        fallback->setObjectName("viewportFallback");
        fallback->setAutoFillBackground(true);
        QPalette palette = fallback->palette();
        palette.setColor(QPalette::Window, QColor(70, 70, 75));
        fallback->setPalette(palette);
        viewport_ = fallback;
    }
    hierarchy_ = new HierarchyPanel(document_, workspace_);
    inspector_ = new InspectorPanel(document_, preferences_, workspace_);
    characterPanel_ = new CharacterPanel(document_, workspace_);
    console_ = new ConsolePanel(workspace_);
    history_ = new HistoryPanel(document_, workspace_);
    timeline_ = new TimelinePanel(document_, actions_, workspace_);
    poses_ = new PoseLibraryPanel(document_, workspace_);
    graph_ = new GraphPanel(document_, workspace_);
    ik_ = new IKPanel(document_, workspace_);
    skin_ = new SkinPanel(document_, workspace_);
    mix_ = new MixPanel(document_, workspace_);
    deformPanel_ = new DeformPanel(document_, workspace_);
    stats_ = new StatsPanel(
        document_,
        [this] {
            FrameStats f;
            if (glViewport_ != nullptr) {
                f.fps = glViewport_->fps();
                f.cpuMs = glViewport_->lastCpuMs();
                f.gpuMs = glViewport_->gpuMs();
            }
            return f;
        },
        workspace_);

    auto* viewportDock = addPanel("dockViewport", viewport_);
    viewportDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    viewportDock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    auto* hierarchyDock = addPanel("dockHierarchy", hierarchy_);
    auto* inspectorDock = addPanel("dockInspector", inspector_);
    auto* characterDock = addPanel("dockCharacter", characterPanel_);
    auto* consoleDock = addPanel("dockConsole", console_);
    auto* historyDock = addPanel("dockHistory", history_);
    auto* timelineDock = addPanel("dockTimeline", timeline_);
    auto* posesDock = addPanel("dockPoses", poses_);
    auto* graphDock = addPanel("dockGraph", graph_);
    auto* ikDock = addPanel("dockIK", ik_);
    auto* skinDock = addPanel("dockSkin", skin_);
    auto* mixDock = addPanel("dockMix", mix_);
    auto* deformDock = addPanel("dockDeform", deformPanel_);
    auto* statsDock = addPanel("dockStats", stats_);

    // Disposition par défaut : hiérarchie à gauche, viewport au centre, inspecteur + personnage à droite,
    // journal en bas.
    ads::CDockAreaWidget* centerArea = dockManager_->setCentralWidget(viewportDock);
    dockManager_->addDockWidget(ads::LeftDockWidgetArea, hierarchyDock, centerArea);
    ads::CDockAreaWidget* rightArea =
        dockManager_->addDockWidget(ads::RightDockWidgetArea, inspectorDock, centerArea);
    dockManager_->addDockWidgetTabToArea(characterDock, rightArea);
    dockManager_->addDockWidgetTabToArea(posesDock, rightArea);
    dockManager_->addDockWidgetTabToArea(ikDock, rightArea);
    dockManager_->addDockWidgetTabToArea(skinDock, rightArea);
    dockManager_->addDockWidgetTabToArea(mixDock, rightArea);
    dockManager_->addDockWidgetTabToArea(deformDock, rightArea);
    inspectorDock->setAsCurrentTab();
    ads::CDockAreaWidget* bottomArea =
        dockManager_->addDockWidget(ads::BottomDockWidgetArea, consoleDock, centerArea);
    dockManager_->addDockWidgetTabToArea(historyDock, bottomArea);
    dockManager_->addDockWidgetTabToArea(statsDock, bottomArea);
    dockManager_->addDockWidgetTabToArea(timelineDock, bottomArea);
    dockManager_->addDockWidgetTabToArea(graphDock, bottomArea);
    timelineDock->setAsCurrentTab();
    dockManager_->setSplitterSizes(centerArea, {260, 760, 320});
    defaultLayout_ = dockManager_->saveState(kLayoutVersion);

    connect(hierarchy_, &HierarchyPanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
    connect(inspector_, &InspectorPanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
    connect(deformPanel_, &DeformPanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
    connect(mix_, &MixPanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
    connect(skin_, &SkinPanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
    connect(ik_, &IKPanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
    connect(poses_, &PoseLibraryPanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
    connect(console_, &ConsolePanel::statusMessage, this, [this](const QString& m) { postStatus(m); });
}

ads::CDockWidget* MainWindow::dock(const QString& objectName) const {
    return dockManager_->findDockWidget(objectName);
}

bool MainWindow::isWorkspaceVisible() const { return stack_->currentWidget() == workspace_; }

void MainWindow::buildActions() {
    const auto act = [this](const QString& id) { return actions_->action(id); };

    // Actions de portée « viewport » : raccourcis actifs seulement quand le viewport (ou un enfant) a le
    // focus.
    for (const ActionRegistry::Entry& entry : actions_->entries()) {
        (entry.viewportScope ? viewport_ : static_cast<QWidget*>(this))->addAction(entry.action);
    }

    // Fichier / Édition / Aide.
    connect(act(ActionId::FileNew), &QAction::triggered, this, &MainWindow::newScene);
    connect(act(ActionId::FileImport), &QAction::triggered, this, &MainWindow::importDialog);
    connect(act(ActionId::FileOpen), &QAction::triggered, this, &MainWindow::openDialog);
    connect(act(ActionId::FileSave), &QAction::triggered, this, &MainWindow::saveProject);
    connect(act(ActionId::FileSaveAs), &QAction::triggered, this, &MainWindow::saveProjectAs);
    connect(act(ActionId::FileExport), &QAction::triggered, this, &MainWindow::exportGltfDialog);
    connect(act(ActionId::FileExportImage), &QAction::triggered, this, &MainWindow::exportImageDialog);
    connect(act(ActionId::FileExportSequence), &QAction::triggered, this, &MainWindow::exportSequenceDialog);
    connect(act(ActionId::FileQuit), &QAction::triggered, this, &QWidget::close);
    connect(act(ActionId::EditPreferences), &QAction::triggered, this, &MainWindow::showPreferences);
    connect(act(ActionId::ViewResetLayout), &QAction::triggered, this, &MainWindow::resetLayout);
    connect(act(ActionId::HelpQuickStart), &QAction::triggered, this, &MainWindow::showQuickStart);
    connect(act(ActionId::HelpAbout), &QAction::triggered, this, &MainWindow::showAbout);

    // Vue : relayées vers le viewport OpenGL (désactivées quand il n'existe pas).
    const QList<QString> viewIds = {ActionId::ViewFocus, ActionId::ViewResetCamera,
                                    ActionId::ViewOrthographic, ActionId::ViewGpuSkinning,
                                    ActionId::ViewInfluence};
    for (const QString& id : viewIds) {
        act(id)->setEnabled(glViewport_ != nullptr);
    }
    act(ActionId::ViewGpuSkinning)->setChecked(true);
    if (glViewport_ != nullptr) {
        connect(act(ActionId::ViewFocus), &QAction::triggered, glViewport_, &ViewportWidget::focusSelection);
        connect(act(ActionId::ViewResetCamera), &QAction::triggered, this, [this] {
            glViewport_->resetCamera();
            actions_->action(ActionId::ViewOrthographic)->setChecked(false);
        });
        connect(act(ActionId::ViewOrthographic), &QAction::toggled, glViewport_,
                &ViewportWidget::setOrthographic);
        connect(act(ActionId::ViewGpuSkinning), &QAction::toggled, glViewport_,
                &ViewportWidget::setGpuSkinning);
        connect(act(ActionId::ViewInfluence), &QAction::toggled, glViewport_,
                &ViewportWidget::setShowInfluence);
        glViewport_->setContextMenuActions({act(ActionId::ViewFocus), act(ActionId::ViewResetCamera),
                                            act(ActionId::ViewOrthographic), act(ActionId::ViewGpuSkinning),
                                            act(ActionId::ViewInfluence)});
        connect(glViewport_, &ViewportWidget::fpsChanged, this, [this](double fps) {
            fpsLabel_->setText(tr("%1 FPS").arg(document_->isPlaying() ? QString::number(fps, 'f', 0)
                                                                       : QStringLiteral("—")));
        });
    }

    // Modes et outils : groupes exclusifs synchronisés avec le Document.
    modeGroup_ = new QActionGroup(this);
    for (const QString& id : {ActionId::ModeRig, ActionId::ModeSkin, ActionId::ModeAnimate}) {
        modeGroup_->addAction(act(id));
    }
    toolGroup_ = new QActionGroup(this);
    for (const QString& id :
         {ActionId::ToolSelect, ActionId::ToolMove, ActionId::ToolRotate, ActionId::ToolScale}) {
        toolGroup_->addAction(act(id));
    }
    connect(act(ActionId::ModeRig), &QAction::triggered, this, [this] { document_->setMode(Mode::Rig); });
    connect(act(ActionId::ModeSkin), &QAction::triggered, this, [this] { document_->setMode(Mode::Skin); });
    connect(act(ActionId::ModeAnimate), &QAction::triggered, this,
            [this] { document_->setMode(Mode::Animate); });
    connect(act(ActionId::ToolSelect), &QAction::triggered, this,
            [this] { document_->setTool(Tool::Select); });
    connect(act(ActionId::ToolMove), &QAction::triggered, this, [this] { document_->setTool(Tool::Move); });
    connect(act(ActionId::ToolRotate), &QAction::triggered, this,
            [this] { document_->setTool(Tool::Rotate); });
    connect(act(ActionId::ToolScale), &QAction::triggered, this, [this] { document_->setTool(Tool::Scale); });

    // Lecture.
    connect(act(ActionId::AnimPlay), &QAction::triggered, document_, &Document::togglePlaying);
    connect(act(ActionId::AnimNextFrame), &QAction::triggered, this, [this] {
        document_->setPlaying(false);
        document_->setFrame(document_->frame() + 1);
    });
    connect(act(ActionId::AnimPrevFrame), &QAction::triggered, this, [this] {
        document_->setPlaying(false);
        document_->setFrame(document_->frame() - 1);
    });

    connect(act(ActionId::AnimFirstFrame), &QAction::triggered, this, [this] {
        document_->setPlaying(false);
        document_->setFrame(0);
    });
    connect(act(ActionId::AnimLastFrame), &QAction::triggered, this, [this] {
        document_->setPlaying(false);
        document_->setFrame(std::max(document_->frameCount() - 1, 0));
    });
    connect(act(ActionId::AnimInsertKey), &QAction::triggered, this,
            [this] { document_->insertKeys(document_->selection()); });
    connect(act(ActionId::AnimToggleIK), &QAction::triggered, this, [this] {
        int index = -1;
        for (const BoneId id : document_->selection()) {
            index = document_->ikControlFor(id);
            if (index >= 0) break;
        }
        if (index < 0 && document_->ikControls().size() == 1)
            index = 0;  // un seul contrôle : pas d'ambiguïté
        if (index >= 0) {
            document_->toggleIK(index);
        } else {
            postStatus(tr("Select a bone of an IK chain (or its target) first"));
        }
    });
    connect(act(ActionId::AnimAutoKey), &QAction::toggled, document_, &Document::setAutoKey);
    connect(document_, &Document::autoKeyChanged, this, [this] {
        actions_->action(ActionId::AnimAutoKey)->setChecked(document_->autoKey());
        refreshStatus();
    });
    connect(act(ActionId::AnimOnionSkin), &QAction::toggled, this, [this](bool on) {
        OnionSkin onion = document_->onionSkin();
        onion.enabled = on;
        document_->setOnionSkin(onion);
    });
    for (const QString& id : {ActionId::AnimToggleIK, ActionId::AnimInsertKey, ActionId::AnimFirstFrame,
                              ActionId::AnimLastFrame, ActionId::AnimAutoKey, ActionId::AnimOnionSkin}) {
        addAction(act(id));  // les raccourcis de fenêtre marchent même si le panneau Timeline est fermé
    }

    // Annuler / rétablir (Phase 8) : textes dynamiques « Annuler Déplacer les os (coude) ».
    connect(act(ActionId::EditUndo), &QAction::triggered, document_, &Document::undo);
    connect(act(ActionId::EditRedo), &QAction::triggered, document_, &Document::redo);
    connect(document_, &Document::historyChanged, this, &MainWindow::updateUndoActions);
    connect(document_, &Document::manipulationChanged, this, [this] { updateUndoActions(); });
    connect(document_, &Document::historyChanged, this, [this] { refreshTitle(); });

    // Sélection, rig, pose, visibilité (Phase 7).
    connect(act(ActionId::SelectAll), &QAction::triggered, document_, &Document::selectAll);
    connect(act(ActionId::SelectNone), &QAction::triggered, document_, &Document::clearSelection);
    connect(act(ActionId::ViewGizmoSpace), &QAction::triggered, document_, &Document::cycleGizmoSpace);
    const auto reportError = [this](const Result<void>& result) {
        if (!result) {
            postStatus(QString::fromStdString(result.error().message));
        }
    };
    connect(act(ActionId::RigAddBone), &QAction::triggered, this,
            [this, reportError] { reportError(document_->addBone()); });
    connect(act(ActionId::RigExtrude), &QAction::triggered, this, [this, reportError] {
        const auto result =
            document_->addBone();  // nouvel os à l'extrémité, puis on passe à l'outil Déplacer
        reportError(result);
        if (result) {
            document_->setTool(Tool::Move);
        }
    });
    connect(act(ActionId::RigSubdivide), &QAction::triggered, this,
            [this, reportError] { reportError(document_->subdivideSelected()); });
    connect(act(ActionId::RigDelete), &QAction::triggered, this,
            [this, reportError] { reportError(document_->deleteSelected(false)); });
    connect(act(ActionId::RigDeleteTree), &QAction::triggered, this,
            [this, reportError] { reportError(document_->deleteSelected(true)); });
    connect(act(ActionId::PoseReset), &QAction::triggered, this, [this] { document_->resetPose(true); });
    connect(act(ActionId::PoseResetAll), &QAction::triggered, this, [this] { document_->resetPose(false); });
    connect(act(ActionId::PoseCopy), &QAction::triggered, this, [this] {
        document_->copyPose(true);
        postStatus(tr("Pose copied"));
    });
    connect(act(ActionId::PosePaste), &QAction::triggered, this, [this] { document_->pastePose(false); });
    connect(act(ActionId::PosePasteMirrored), &QAction::triggered, this,
            [this] { document_->pastePose(true); });
    connect(act(ActionId::PoseSymmetry), &QAction::toggled, document_, &Document::setSymmetryX);
    connect(act(ActionId::BoneHide), &QAction::triggered, document_, &Document::hideSelected);
    connect(act(ActionId::BoneShowAll), &QAction::triggered, document_, &Document::showAll);
    connect(act(ActionId::BoneIsolate), &QAction::toggled, document_, &Document::setIsolated);
    connect(act(ActionId::BoneLock), &QAction::triggered, document_, &Document::lockSelected);
    connect(act(ActionId::BoneUnlockAll), &QAction::triggered, document_, &Document::unlockAll);
    connect(document_, &Document::symmetryChanged, this, [this] {
        actions_->action(ActionId::PoseSymmetry)->setChecked(document_->symmetryX());
        refreshStatus();
    });
    connect(document_, &Document::visibilityChanged, this,
            [this] { actions_->action(ActionId::BoneIsolate)->setChecked(document_->isIsolated()); });
    connect(document_, &Document::gizmoSpaceChanged, this, &MainWindow::refreshStatus);
    connect(document_, &Document::warning, this, [this](const QString& message) {
        spdlog::warn("{}", message.toStdString());
        postStatus(message, 12000);
    });
    connect(document_, &Document::modeChanged, this, [this] { updateActionStates(); });
    connect(document_, &Document::selectionChanged, this, &MainWindow::updateActionStates);
    connect(document_, &Document::characterChanged, this, &MainWindow::updateActionStates);
    connect(document_, &Document::hierarchyChanged, this, &MainWindow::updateActionStates);
    connect(document_, &Document::ikChanged, this, &MainWindow::updateActionStates);
    updateActionStates();
}

void MainWindow::buildMenus() {
    const auto act = [this](const QString& id) { return actions_->action(id); };

    fileMenu_ = menuBar()->addMenu(QString());
    fileMenu_->setObjectName("menuFile");
    fileMenu_->addAction(act(ActionId::FileNew));
    fileMenu_->addAction(act(ActionId::FileOpen));
    recentMenu_ = fileMenu_->addMenu(QString());
    recentMenu_->setObjectName("menuRecent");
    fileMenu_->addAction(act(ActionId::FileSave));
    fileMenu_->addAction(act(ActionId::FileSaveAs));
    fileMenu_->addSeparator();
    fileMenu_->addAction(act(ActionId::FileImport));
    fileMenu_->addAction(act(ActionId::FileExport));
    fileMenu_->addAction(act(ActionId::FileExportImage));
    fileMenu_->addAction(act(ActionId::FileExportSequence));
    fileMenu_->addSeparator();
    fileMenu_->addAction(act(ActionId::FileQuit));

    editMenu_ = menuBar()->addMenu(QString());
    editMenu_->setObjectName("menuEdit");
    editMenu_->addAction(act(ActionId::EditUndo));
    editMenu_->addAction(act(ActionId::EditRedo));
    editMenu_->addSeparator();
    selectMenu_ = editMenu_->addMenu(QString());
    selectMenu_->addAction(act(ActionId::SelectAll));
    selectMenu_->addAction(act(ActionId::SelectNone));
    boneMenu_ = editMenu_->addMenu(QString());
    boneMenu_->addAction(act(ActionId::RigAddBone));
    boneMenu_->addAction(act(ActionId::RigExtrude));
    boneMenu_->addAction(act(ActionId::RigSubdivide));
    boneMenu_->addAction(act(ActionId::RigDelete));
    boneMenu_->addAction(act(ActionId::RigDeleteTree));
    poseMenu_ = editMenu_->addMenu(QString());
    poseMenu_->addAction(act(ActionId::PoseReset));
    poseMenu_->addAction(act(ActionId::PoseResetAll));
    poseMenu_->addSeparator();
    poseMenu_->addAction(act(ActionId::PoseCopy));
    poseMenu_->addAction(act(ActionId::PosePaste));
    poseMenu_->addAction(act(ActionId::PosePasteMirrored));
    poseMenu_->addSeparator();
    poseMenu_->addAction(act(ActionId::PoseSymmetry));
    visibilityMenu_ = editMenu_->addMenu(QString());
    visibilityMenu_->addAction(act(ActionId::BoneHide));
    visibilityMenu_->addAction(act(ActionId::BoneShowAll));
    visibilityMenu_->addAction(act(ActionId::BoneIsolate));
    visibilityMenu_->addAction(act(ActionId::BoneLock));
    visibilityMenu_->addAction(act(ActionId::BoneUnlockAll));
    editMenu_->addSeparator();
    editMenu_->addAction(act(ActionId::EditPreferences));

    viewMenu_ = menuBar()->addMenu(QString());
    viewMenu_->setObjectName("menuView");
    panelsMenu_ = viewMenu_->addMenu(QString());
    panelsMenu_->setObjectName("menuPanels");
    for (const char* name :
         {"dockHierarchy", "dockInspector", "dockCharacter", "dockConsole", "dockHistory", "dockTimeline",
          "dockGraph", "dockPoses", "dockIK", "dockSkin", "dockMix", "dockDeform", "dockStats"}) {
        panelsMenu_->addAction(dock(QString::fromLatin1(name))->toggleViewAction());
    }
    viewMenu_->addSeparator();
    viewMenu_->addAction(act(ActionId::ViewFocus));
    viewMenu_->addAction(act(ActionId::ViewResetCamera));
    viewMenu_->addAction(act(ActionId::ViewOrthographic));
    viewMenu_->addAction(act(ActionId::ViewGpuSkinning));
    viewMenu_->addAction(act(ActionId::ViewInfluence));
    viewMenu_->addAction(act(ActionId::ViewGizmoSpace));
    viewMenu_->addSeparator();
    viewMenu_->addAction(act(ActionId::ViewResetLayout));

    helpMenu_ = menuBar()->addMenu(QString());
    helpMenu_->setObjectName("menuHelp");
    helpMenu_->addAction(act(ActionId::HelpQuickStart));
    helpMenu_->addAction(act(ActionId::HelpAbout));

    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();
}

void MainWindow::buildToolBars() {
    const auto act = [this](const QString& id) { return actions_->action(id); };
    modeBar_ = addToolBar(QString());
    modeBar_->setObjectName("modeToolBar");
    modeBar_->setMovable(false);
    modeBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (const QString& id : {ActionId::ModeRig, ActionId::ModeSkin, ActionId::ModeAnimate}) {
        modeBar_->addAction(act(id));
    }
    modeBar_->addSeparator();
    for (const QString& id :
         {ActionId::ToolSelect, ActionId::ToolMove, ActionId::ToolRotate, ActionId::ToolScale}) {
        modeBar_->addAction(act(id));
    }
    modeBar_->addAction(act(ActionId::PoseSymmetry));
    modeBar_->addSeparator();
    modeBar_->addAction(act(ActionId::AnimPrevFrame));
    modeBar_->addAction(act(ActionId::AnimPlay));
    modeBar_->addAction(act(ActionId::AnimNextFrame));
    // Les boutons d'outils et de lecture n'affichent que l'icône (le texte est dans l'info-bulle).
    for (const QString& id :
         {ActionId::ToolSelect, ActionId::ToolMove, ActionId::ToolRotate, ActionId::ToolScale,
          ActionId::PoseSymmetry, ActionId::AnimPrevFrame, ActionId::AnimPlay, ActionId::AnimNextFrame}) {
        if (auto* button = qobject_cast<QToolButton*>(modeBar_->widgetForAction(act(id)))) {
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        }
    }
}

void MainWindow::buildStatusBar() {
    modeLabel_ = new QLabel(this);
    modeLabel_->setObjectName("statusMode");
    toolLabel_ = new QLabel(this);
    toolLabel_->setObjectName("statusTool");
    frameLabel_ = new QLabel(this);
    frameLabel_->setObjectName("statusFrame");
    fpsLabel_ = new QLabel(this);
    fpsLabel_->setObjectName("statusFps");
    statsLabel_ = new QLabel(this);
    statsLabel_->setObjectName("statusStats");
    progressBar_ = new QProgressBar(this);
    progressBar_->setObjectName("statusProgress");
    progressBar_->setRange(0, 100);
    progressBar_->setMaximumWidth(160);
    progressBar_->setVisible(false);
    statusBar()->addPermanentWidget(progressBar_);
    for (QLabel* label : {modeLabel_, toolLabel_, frameLabel_, fpsLabel_, statsLabel_}) {
        statusBar()->addPermanentWidget(label);
    }
}

void MainWindow::connectDocument() {
    connect(document_, &Document::modeChanged, this, [this](Mode mode) {
        const QString id = mode == Mode::Rig
                               ? ActionId::ModeRig
                               : (mode == Mode::Skin ? ActionId::ModeSkin : ActionId::ModeAnimate);
        actions_->action(id)->setChecked(true);
        refreshStatus();
    });
    connect(document_, &Document::toolChanged, this, [this](Tool tool) {
        const QString id = tool == Tool::Select   ? ActionId::ToolSelect
                           : tool == Tool::Move   ? ActionId::ToolMove
                           : tool == Tool::Rotate ? ActionId::ToolRotate
                                                  : ActionId::ToolScale;
        actions_->action(id)->setChecked(true);
        refreshStatus();
    });
    connect(document_, &Document::timeChanged, this, &MainWindow::refreshStatus);
    connect(document_, &Document::clipChanged, this, &MainWindow::refreshStatus);
    connect(document_, &Document::selectionChanged, this, &MainWindow::refreshStatus);
    connect(document_, &Document::characterChanged, this, [this] {
        refreshStatus();
        refreshTitle();
    });
    connect(document_, &Document::playingChanged, this, [this](bool playing) {
        actions_->action(ActionId::AnimPlay)
            ->setIcon(Icons::get(playing ? QStringLiteral("pause") : QStringLiteral("play")));
    });
    actions_->action(ActionId::ModeAnimate)->setChecked(true);
    actions_->action(ActionId::ToolSelect)->setChecked(true);
}

// --- Retraduction / thème -------------------------------------------------------------------

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange && constructed_) {
        retranslate();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslate() {
    actions_->retranslate();
    updateUndoActions();
    fileMenu_->setTitle(tr("&File"));
    recentMenu_->setTitle(tr("Open &Recent"));
    editMenu_->setTitle(tr("&Edit"));
    viewMenu_->setTitle(tr("&View"));
    panelsMenu_->setTitle(tr("&Panels"));
    selectMenu_->setTitle(tr("&Select"));
    boneMenu_->setTitle(tr("&Bone"));
    poseMenu_->setTitle(tr("&Pose"));
    visibilityMenu_->setTitle(tr("&Visibility"));
    helpMenu_->setTitle(tr("&Help"));
    modeBar_->setWindowTitle(tr("Modes and tools"));
    const struct {
        const char* name;
        QString title;
    } titles[] = {{"dockViewport", tr("Viewport")},   {"dockHierarchy", tr("Hierarchy")},
                  {"dockInspector", tr("Inspector")}, {"dockCharacter", tr("Character")},
                  {"dockConsole", tr("Console")},     {"dockHistory", tr("History")},
                  {"dockTimeline", tr("Timeline")},   {"dockPoses", tr("Poses")},
                  {"dockGraph", tr("Curves")},        {"dockIK", tr("IK")},
                  {"dockSkin", tr("Skin")},           {"dockMix", tr("Mix")},
                  {"dockDeform", tr("Deform")},       {"dockStats", tr("Statistics")}};
    for (const auto& entry : titles) {
        if (auto* d = dock(QString::fromLatin1(entry.name))) {
            d->setWindowTitle(entry.title);
            d->toggleViewAction()->setText(entry.title);
        }
    }
    rebuildRecentMenu();
    refreshStatus();
    refreshTitle();
}

void MainWindow::applyDockTheme() {
    dockManager_->setStyleSheet(adsBaseStyleSheet_ + Theme::dockStyleSheet(Theme::colors()));
}

void MainWindow::applyPreferenceChange(const QString& key) {
    if (key == QLatin1String("fontSize")) {
        Theme::setFontSize(preferences_->fontSize());
        if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
            app->setStyleSheet(
                Theme::styleSheet(Theme::colors()));  // recalcule les métriques dépendant de la police
        }
        return;
    }
    if (key == QLatin1String("autosave")) {
        autosaveTimer_->setInterval(preferences_->autosaveMinutes() * 60 * 1000);
        if (preferences_->autosaveEnabled()) {
            autosaveTimer_->start();
        } else {
            autosaveTimer_->stop();
        }
        return;
    }
    if (key == QLatin1String("theme")) {
        if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
            Theme::apply(*app, preferences_->theme());
        }
        applyDockTheme();
        actions_->refreshIcons();
        actions_->action(ActionId::AnimPlay)
            ->setIcon(Icons::get(document_->isPlaying() ? QStringLiteral("pause") : QStringLiteral("play")));
        welcome_->retranslate();
        console_->retranslate();
        hierarchy_->retranslate();
        characterPanel_->retranslate();
        inspector_->retranslate();
    } else if (key == QLatin1String("language")) {
        applyLanguage(preferences_->language());  // QApplication envoie LanguageChange à tous les widgets
    } else if (key == QLatin1String("defaultFps")) {
        document_->setDefaultFps(preferences_->defaultFps());
    }
    // L'unité d'angle est relue par l'inspecteur ; l'autosauvegarde sera branchée avec les projets (Phase
    // 11).
}

// --- Statut ---------------------------------------------------------------------------------

void MainWindow::updateUndoActions() {
    QAction* undo = actions_->action(ActionId::EditUndo);
    QAction* redo = actions_->action(ActionId::EditRedo);
    undo->setEnabled(document_->canUndo());
    redo->setEnabled(document_->canRedo());
    undo->setText(document_->canUndo() ? tr("&Undo %1").arg(document_->undoText()) : tr("&Undo"));
    redo->setText(document_->canRedo() ? tr("&Redo %1").arg(document_->redoText()) : tr("&Redo"));
}

void MainWindow::updateActionStates() {
    const bool has = document_->hasCharacter();
    const bool rig = has && document_->mode() == Mode::Rig;
    const bool pose = has && document_->mode() != Mode::Rig;
    const bool selected = has && !document_->selection().empty();
    const auto enable = [this](const QString& id, bool on) { actions_->action(id)->setEnabled(on); };
    enable(ActionId::SelectAll, has);
    enable(ActionId::SelectNone, selected);
    enable(ActionId::RigAddBone, rig);
    enable(ActionId::RigExtrude, rig && selected);
    enable(ActionId::RigSubdivide, rig && selected);
    enable(ActionId::RigDelete, rig && selected);
    enable(ActionId::RigDeleteTree, rig && selected);
    enable(ActionId::PoseReset, pose && selected);
    enable(ActionId::PoseResetAll, pose);
    enable(ActionId::PoseCopy, has);
    enable(ActionId::PosePaste, has);
    enable(ActionId::PosePasteMirrored, has);
    enable(ActionId::BoneHide, selected);
    enable(ActionId::BoneShowAll, has);
    enable(ActionId::BoneIsolate, selected || document_->isIsolated());
    enable(ActionId::BoneLock, selected);
    enable(ActionId::BoneUnlockAll, has);
    const bool animate = has && document_->mode() == Mode::Animate;
    enable(ActionId::AnimInsertKey, animate && selected);
    enable(ActionId::AnimAutoKey, has);
    enable(ActionId::FileSave, has);
    enable(ActionId::FileSaveAs, has);
    enable(ActionId::FileExport, has);
    enable(ActionId::FileExportImage, has && glViewport_ != nullptr);
    enable(ActionId::FileExportSequence, has && glViewport_ != nullptr);
    enable(ActionId::AnimToggleIK, animate && !document_->ikControls().empty());
    enable(ActionId::AnimOnionSkin, has);
    enable(ActionId::AnimFirstFrame, has);
    enable(ActionId::AnimLastFrame, has);
}

void MainWindow::postStatus(const QString& message, int timeoutMs) {
    statusBar()->showMessage(message, timeoutMs);
}

void MainWindow::refreshStatus() {
    const QString mode = document_->mode() == Mode::Rig
                             ? tr("Rig")
                             : (document_->mode() == Mode::Skin ? tr("Skin") : tr("Animate"));
    QString tool;
    switch (document_->tool()) {
        case Tool::Select:
            tool = tr("Select");
            break;
        case Tool::Move:
            tool = tr("Move");
            break;
        case Tool::Rotate:
            tool = tr("Rotate");
            break;
        case Tool::Scale:
            tool = tr("Scale");
            break;
    }
    modeLabel_->setText(tr("Mode: %1").arg(mode));
    QString space;
    switch (document_->gizmoSpace()) {
        case GizmoSpace::Local:
            space = tr("Local");
            break;
        case GizmoSpace::Global:
            space = tr("Global");
            break;
        case GizmoSpace::Parent:
            space = tr("Parent");
            break;
    }
    toolLabel_->setText(document_->tool() == Tool::Select ? tr("Tool: %1").arg(tool)
                                                          : tr("Tool: %1 (%2)").arg(tool, space));
    if (document_->symmetryX()) {
        toolLabel_->setText(toolLabel_->text() + tr(" - Mirror X"));
    }
    if (document_->clip() != nullptr) {
        frameLabel_->setText(
            tr("Frame %1 / %2").arg(document_->frame()).arg(std::max(document_->frameCount() - 1, 0)));
    } else {
        frameLabel_->setText(tr("Frame %1").arg(QStringLiteral("—")));
    }
    if (document_->autoKey()) {
        frameLabel_->setText(frameLabel_->text() + tr(" - Auto Key"));
    }
    if (glViewport_ == nullptr) {
        fpsLabel_->setText(tr("%1 FPS").arg(QStringLiteral("—")));
    }
    if (const auto character = document_->character()) {
        statsLabel_->setText(tr("%1 bones, %2 vertices, %3 selected")
                                 .arg(character->skeleton.boneCount())
                                 .arg(document_->vertexCount())
                                 .arg(document_->selection().size()));
    } else {
        statsLabel_->setText(tr("No character"));
    }
}

void MainWindow::refreshTitle() {
    const QString banner = QString::fromStdString(versionBanner());
    const auto character = document_->character();
    const QString name = !projectPath_.isEmpty()
                             ? QFileInfo(projectPath_).completeBaseName()
                             : (character ? QString::fromStdString(character->name) : QString());
    setWindowTitle((character ? QStringLiteral("%1 - %2").arg(name, banner) : banner) +
                   QStringLiteral("[*]"));
    setWindowModified(character != nullptr &&
                      !document_->isClean());  // « * » : modifications non sauvegardées
}

// --- Fichiers -------------------------------------------------------------------------------

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    const QStringList recent = preferences_->recentFiles();
    for (const QString& path : recent) {
        QAction* action = recentMenu_->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        action->setStatusTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openRecentPath(path); });
    }
    if (recent.isEmpty()) {
        recentMenu_->addAction(tr("(no recent file)"))->setEnabled(false);
    } else {
        recentMenu_->addSeparator();
        recentMenu_->addAction(tr("Clear list"), preferences_, &Preferences::clearRecentFiles);
    }
}

void MainWindow::showWorkspace() { stack_->setCurrentWidget(workspace_); }

void MainWindow::openRecentPath(const QString& path) {
    if (!confirmDiscard()) return;
    if (QFileInfo(path).suffix().compare(QStringLiteral("rig"), Qt::CaseInsensitive) == 0) {
        if (const auto opened = openProject(path); !opened) {
            postStatus(tr("Cannot open the project: %1").arg(QString::fromStdString(opened.error().message)),
                       10000);
        }
    } else {
        importFileAsync(path);
    }
}

void MainWindow::newScene() {
    if (!confirmDiscard()) return;
    projectPath_.clear();
    discardAutosave();
    document_->setCharacter(nullptr);
    document_->markClean();
    showWorkspace();
    postStatus(tr("New empty scene"));
}

void MainWindow::importDialog() {
    const auto extensions = rigger::io::supportedImportExtensions();
    QStringList patterns;
    for (const std::string_view ext : extensions) {
        patterns << QStringLiteral("*") + QString::fromUtf8(ext.data(), static_cast<qsizetype>(ext.size()));
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import a 3D model"), QString(), tr("glTF 2.0 (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (!path.isEmpty() && confirmDiscard()) {
        importFileAsync(path);
    }
}

std::shared_ptr<const Character> MainWindow::character() const { return document_->character(); }

Result<void> MainWindow::importFile(const QString& path) {
    auto imported = io::importGltf(std::filesystem::path(path.toStdWString()));
    if (!imported) {
        return imported.error();
    }
    finishImport(std::make_shared<Result<Character>>(std::move(imported)), path);
    return {};
}

void MainWindow::importFileAsync(const QString& path) {
    if (importTask_ != nullptr) {
        postStatus(tr("An import is already in progress"));
        return;
    }
    auto result = std::make_shared<Result<Character>>(Error{"import did not run"});
    const std::filesystem::path fsPath(path.toStdWString());
    importTask_ = new BackgroundTask(
        [result, fsPath](TaskContext& context) {
            *result = io::importGltf(fsPath, [&context](float fraction, std::string_view stage) {
                context.report(static_cast<double>(fraction),
                               QString::fromUtf8(stage.data(), static_cast<qsizetype>(stage.size())));
                return !context.isCancelled();
            });
        },
        this);

    progressDialog_ = new QProgressDialog(tr("Importing %1...").arg(QFileInfo(path).fileName()), tr("Cancel"),
                                          0, 100, this);
    progressDialog_->setObjectName("importProgressDialog");
    progressDialog_->setWindowModality(Qt::WindowModal);
    progressDialog_->setMinimumDuration(300);  // un import rapide n'affiche pas de boîte de dialogue
    progressDialog_->setAutoClose(false);
    progressDialog_->setAutoReset(false);
    progressDialog_->setValue(0);
    connect(progressDialog_, &QProgressDialog::canceled, importTask_, &BackgroundTask::cancel);

    progressBar_->setValue(0);
    progressBar_->setVisible(true);
    connect(importTask_, &BackgroundTask::progress, this, [this](double fraction, const QString& stage) {
        const int percent = static_cast<int>(fraction * 100.0);
        const QPointer<QProgressDialog> dialog = progressDialog_;
        progressBar_->setValue(percent);
        postStatus(stageText(stage), 0);
        if (dialog) {
            dialog->setLabelText(stageText(stage));
            // En dernier : setValue() d'une boîte modale traite des événements (la fin de l'import peut donc
            // survenir pendant cet appel ; rien ne doit être touché ensuite).
            dialog->setValue(percent);
        }
    });
    connect(importTask_, &BackgroundTask::finished, this, [this, result, path] {
        BackgroundTask* task = importTask_;
        importTask_ = nullptr;
        task->deleteLater();
        if (progressDialog_ != nullptr) {
            progressDialog_->close();
            progressDialog_->deleteLater();
            progressDialog_ = nullptr;
        }
        progressBar_->setVisible(false);
        finishImport(result, path);
    });
    importTask_->start();
}

void MainWindow::cancelImport() {
    if (importTask_ != nullptr) {
        importTask_->cancel();
    }
}

void MainWindow::finishImport(const std::shared_ptr<Result<Character>>& result, const QString& path) {
    if (!result->ok()) {
        const QString message = QString::fromStdString(result->error().message);
        if (result->error().message == io::kImportCancelled) {
            postStatus(tr("Import cancelled"));
        } else {
            postStatus(tr("Import failed: %1").arg(message));
            ++reportedErrors_;
            spdlog::error("import of '{}' failed: {}", path.toStdString(), result->error().message);
            // Non bloquant (open() et non exec()) : l'interface et les tests restent réactifs.
            auto* box =
                new QMessageBox(QMessageBox::Warning, tr("Import failed"), message, QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        }
        emit importFinished(false, message);
        return;
    }
    auto character = std::make_shared<Character>(std::move(result->value()));
    const QString name = QFileInfo(path).fileName();
    QString message = tr("Imported %1: %2 bones, %3 meshes, %4 animations")
                          .arg(name)
                          .arg(character->skeleton.boneCount())
                          .arg(character->meshes.size())
                          .arg(character->clips.size());
    if (!character->warnings.empty()) {
        message += tr(" (%1 warnings, see the console)").arg(character->warnings.size());
    }
    document_->setCharacter(std::move(character), path);
    document_->markClean();
    projectPath_.clear();  // un import n'est pas un projet : « Enregistrer » demandera un nom
    preferences_->addRecentFile(path);
    showWorkspace();
    postStatus(message);
    emit importFinished(true, message);
}

bool MainWindow::importDropped(const QMimeData& mime) {
    for (const QUrl& url : mime.urls()) {
        if (url.isLocalFile() && isSupportedImportFile(url.toLocalFile())) {
            importFileAsync(url.toLocalFile());  // un seul fichier à la fois : le premier supporté
            return true;
        }
    }
    return false;
}

QImage MainWindow::grabViewport() {
    return glViewport_ != nullptr ? glViewport_->grabFramebuffer() : QImage();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const auto urls = event->mimeData()->urls();
    const bool supported = std::any_of(urls.begin(), urls.end(), [](const QUrl& url) {
        return url.isLocalFile() && isSupportedImportFile(url.toLocalFile());
    });
    if (supported) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (importDropped(*event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

// --- Disposition ----------------------------------------------------------------------------

void MainWindow::resetLayout() {
    if (!defaultLayout_.isEmpty()) {
        dockManager_->restoreState(defaultLayout_, kLayoutVersion);
    }
    postStatus(tr("Layout reset"));
}

void MainWindow::saveLayout() {
    preferences_->setLayoutState(dockManager_->saveState(kLayoutVersion));
    preferences_->setWindowGeometry(saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (importTask_ != nullptr) {
        importTask_->cancel();
    }
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    discardAutosave();  // sortie propre : rien à récupérer
    saveLayout();
    QMainWindow::closeEvent(event);
}

// --- Dialogues ------------------------------------------------------------------------------

void MainWindow::showPreferences() {
    if (preferencesDialog_ == nullptr) {
        preferencesDialog_ = new PreferencesDialog(preferences_, actions_, this);
    }
    preferencesDialog_->show();
    preferencesDialog_->raise();
    preferencesDialog_->activateWindow();
}

void MainWindow::showQuickStart() {
    QMessageBox::information(
        this, tr("Quick start"),
        tr("<h3>Quick start</h3>"
           "<ol>"
           "<li>New here? Click <b>Open the example</b> on the welcome screen for a ready-made "
           "character.</li><li><b>Import</b> a glTF / GLB character with File &gt; Import... or drop the "
           "file on the "
           "window.</li>"
           "<li>Pick a <b>mode</b> with the toolbar: Rig (1), Skin (2) or Animate (3).</li>"
           "<li><b>Select</b> bones in the Hierarchy panel; edit them in the Inspector (drag a number to "
           "scrub it, "
           "double-click to type).</li>"
           "<li><b>Navigate</b> the viewport: Alt + left drag to orbit, Alt + middle drag to pan, wheel to "
           "zoom, "
           "F to frame the selection.</li>"
           "<li>Press <b>Space</b> to play or pause the embedded animation.</li>"
           "</ol>"));
}

void MainWindow::openExample(bool withTutorial) {
    if (!confirmDiscard()) return;
    const QDir app(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        app.filePath(QStringLiteral("../share/rigger/examples/first_steps.rig")),
        app.filePath(QStringLiteral("examples/first_steps.rig")),
        app.filePath(QStringLiteral("../../../assets/examples/first_steps.rig")),
        app.filePath(QStringLiteral("../../../../assets/examples/first_steps.rig"))};
    bool opened = false;
    for (const QString& path : candidates) {
        if (QFile::exists(path) && openProject(QDir::cleanPath(path))) {
            opened = true;
            break;
        }
    }
    if (!opened) {  // fichier absent : le personnage est généré par le code
        document_->setCharacter(std::make_shared<Character>(buildExampleCharacter()), QString());
        document_->setMode(Mode::Animate);
        document_->setClipIndex(0);
        document_->markClean();
        showWorkspace();
    }
    projectPath_.clear();  // l'exemple n'est jamais écrasé : Enregistrer demande un nouveau nom
    refreshTitle();
    postStatus(tr("Example opened: press Space to play, then try the Wave and Walk clips."));
    if (withTutorial) startTutorial();
}

void MainWindow::startTutorial() {
    if (tutorial_ == nullptr) tutorial_ = new TutorialGuide(this);
    tutorial_->setSteps({
        {glViewport_, tr("This is the 3D view. Alt + left drag orbits, Alt + middle drag pans, the wheel "
                         "zooms and F frames the selection.")},
        {hierarchy_,
         tr("The Hierarchy lists the bones. Click one to select it, then edit it in the Inspector.")},
        {glViewport_, tr("Three modes: Rig (1) edits the skeleton, Skin (2) paints the weights and Animate "
                         "(3) poses and keys the character. Q, W, E and R pick the tool.")},
        {timeline_, tr("Press Space to play the Wave clip. Pick a frame, pose a bone and press K to insert a "
                       "key; Ctrl+K turns on auto-key.")},
        {menuBar(), tr("When you are done, File > Export writes a glTF file (Ctrl+Shift+E) and Ctrl+S saves "
                       "the project. Help > Quick start shows these tips again.")},
    });
    tutorial_->start();
}

void MainWindow::showAbout() {
    AboutDialog dialog(QString::fromStdString(render::describeRequirements()), this);
    dialog.exec();
}

}  // namespace rigger::editor
