// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

// clang-format off
// glad doit être inclus AVANT tout en-tête OpenGL de Qt : ne pas laisser clang-format réordonner.
#include "render/mesh_renderer.hpp"
// clang-format on

#include <QElapsedTimer>
#include <QList>
#include <QOpenGLWidget>
#include <QTimer>
#include <memory>
#include <vector>

#include "core/math/ray.hpp"
#include "core/skeleton/picking.hpp"
#include "core/skeleton/skeleton.hpp"
#include "render/camera.hpp"
#include "render/gizmo.hpp"
#include "render/grid.hpp"
#include "render/mesh_renderer.hpp"
#include "render/shader.hpp"
#include "render/skeleton_renderer.hpp"

class QAction;

namespace rigger::editor {

class Document;

// Viewport 3D : affiche le personnage du Document (pose, sélection, méthode de skinning), ou une scène de
// démonstration (bras à 2 os) tant qu'aucun personnage n'est chargé. Les raccourcis clavier viennent des
// QAction de l'ActionRegistry ; le widget expose les slots correspondants.
class ViewportWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setDocument(Document* document);

    // Mode export (images / vidéo) : fond transparent, sans grille, sans squelette ni gizmos.
    void setExportMode(bool transparent, bool hideOverlays) {
        exportTransparent_ = transparent;
        exportHideOverlays_ = hideOverlays;
        update();
    }
    double lastCpuMs() const { return lastCpuMs_; }
    double gpuMs() const { return gpuMs_; }
    rigger::render::Camera& camera() { return camera_; }
    const rigger::render::Camera& camera() const { return camera_; }

    // Actions proposées par le menu contextuel (clic droit sans Alt, sans glissé).
    void setContextMenuActions(const QList<QAction*>& actions) { contextActions_ = actions; }

    bool gpuSkinning() const { return gpuSkinning_; }
    bool showInfluence() const { return showInfluence_; }
    double fps() const { return fps_; }

public slots:
    void focusSelection();
    void resetCamera();
    void setOrthographic(bool enabled);
    void setGpuSkinning(bool enabled);
    void setShowInfluence(bool enabled);

signals:
    void fpsChanged(double fps);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;

private:
    void buildShaderProgram();
    void buildGrid();
    void buildSkinnedArm();
    void drawGrid();
    void drawSkinnedArm();
    void drawSkeleton();
    void rebuildCharacterRenderers();
    void drawCharacter();
    void defaultCamera();
    void updateCursor();
    void countFrame();
    // Chronométrage du dernier rendu : temps CPU de paintGL et temps GPU (requête GL_TIME_ELAPSED, résultat
    // décalé d'une image pour ne jamais bloquer le pipeline).
    double lastCpuMs_ = 0.0;
    double gpuMs_ = 0.0;
    unsigned queries_[2] = {0, 0};
    bool queryPending_[2] = {false, false};
    int queryIndex_ = 0;
    bool queriesReady_ = false;
    // Interaction : picking, sélection, gizmo.
    glm::mat4 viewProjectionMatrix() const;
    Ray rayAt(const QPoint& position) const;
    rigger::PickParams pickParams() const;
    bool gizmoVisible() const;
    bool updateGizmoFrame();  // false si rien à manipuler
    void updateHover(const QPoint& position);
    void finishClick(const QMouseEvent* event);
    void finishRectangle(const QMouseEvent* event);
    void drawOverlays();

    Document* document_ = nullptr;
    rigger::render::Camera camera_;
    QPoint lastPos_;
    QPoint pressPos_;
    bool orbiting_ = false;
    bool panning_ = false;
    bool altDown_ = false;
    rigger::render::Shader shader_;
    rigger::render::Grid grid_;
    rigger::render::MeshRenderer armRenderer_;
    rigger::render::SkeletonRenderer skeletonRenderer_;
    int lastWidth_ = 0;
    int lastHeight_ = 0;
    bool gpuSkinning_ = true;
    bool showInfluence_ = false;
    rigger::Skeleton demoSkeleton_;
    rigger::Pose demoPose_;
    rigger::BoneId forearmBone_ = rigger::kNoBone;

    std::vector<rigger::render::MeshRenderer> characterMeshes_;
    std::vector<char> characterMeshSkinned_;
    bool characterDirty_ = false;
    QList<QAction*> contextActions_;

    // Interaction.
    rigger::render::Gizmo gizmo_;
    rigger::render::MeshRenderer gizmoRenderer_;
    rigger::render::SkeletonRenderer rectRenderer_;
    rigger::BoneId hoverBone_ = rigger::kNoBone;
    bool leftPressed_ = false;
    bool rectSelecting_ = false;
    // Weight painting (mode Skin)
    std::vector<std::vector<float>> morphCache_;  // poids de blend shapes téléversés, par maillage
    void uploadMesh(std::size_t index, const std::vector<float>& weights);
    bool exportTransparent_ = false;
    bool exportHideOverlays_ = false;
    bool painting_ = false;
    bool brushValid_ = false;
    glm::vec3 brushPoint_{0.0f};
    bool skinPaintActive() const;
    void paintDab(const QPoint& position);
    void updateBrushPreview(const QPoint& position);
    enum class SelectionKind { Click, Rectangle, Lasso };
    void finishVertexSelection(const QMouseEvent* event, SelectionKind kind);
    std::vector<QPoint> lasso_;
    bool gizmoDragging_ = false;
    QPoint rectEnd_;
    QPoint lastClickPos_{-1000, -1000};
    std::vector<rigger::PickHit> lastClickHits_;

    QTimer fpsTimer_;
    int framesSinceReport_ = 0;
    QElapsedTimer fpsClock_;
    double fps_ = 0.0;
};

}  // namespace rigger::editor
