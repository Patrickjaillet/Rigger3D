// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/ViewportWidget.hpp"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPlainTextEdit>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/animation/clip.hpp"
#include "core/deform/morph.hpp"
#include "core/skeleton/fk.hpp"
#include "core/skeleton/picking.hpp"
#include "core/util/profiler.hpp"
#include "editor/Document.hpp"
#include "render/gl_loader.hpp"
#include "render/gl_state.hpp"
#include "render/skinning_shader.hpp"

namespace rigger::editor {

namespace {

const std::string kVertexShaderSource = std::string(R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform mat4 uViewProjection;
uniform int uSkinningEnabled;
uniform int uSelectedBone;
out vec3 vColor;
out vec3 vNormal;
out float vInfluence;
)") + std::string(rigger::render::kSkinningGlsl) +
                                        R"(
void main() {
    vColor = aColor;
    vec3 position = uSkinningEnabled != 0 ? skinPosition(aPos, aJoints, aWeights) : aPos;
    vNormal = uSkinningEnabled != 0 ? skinNormal(aNormal, aJoints, aWeights) : normalize(aNormal);
    vInfluence = uSelectedBone >= 0
        ? boneInfluence(aJoints, aWeights, uint(uSelectedBone))
        : 0.0;
    gl_Position = uViewProjection * vec4(position, 1.0);
})";

const char* kFragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
in vec3 vNormal;
in float vInfluence;
uniform int uShowInfluence;
out vec4 FragColor;
void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(0.8, 1.0, 0.6));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    vec3 baseColor = uShowInfluence != 0
        ? mix(vec3(0.05, 0.2, 1.0), vec3(1.0, 0.05, 0.0), clamp(vInfluence, 0.0, 1.0))
        : vColor;
    vec3 color = baseColor * (0.25 + diffuse * 0.75);
    FragColor = vec4(color * lightColor, 1.0);
})";

}  // namespace

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setObjectName("viewportWidget");
    setFocusPolicy(Qt::StrongFocus);
    defaultCamera();
    setMouseTracking(true);
    setToolTip(tr("Alt + left drag: orbit - Alt + middle/right drag: pan - mouse wheel: zoom"));
    fpsClock_.start();
    fpsTimer_.setInterval(500);
    connect(&fpsTimer_, &QTimer::timeout, this, [this] {
        const double seconds = static_cast<double>(fpsClock_.restart()) / 1000.0;
        fps_ = seconds > 0.0 ? framesSinceReport_ / seconds : 0.0;
        framesSinceReport_ = 0;
        emit fpsChanged(fps_);
    });
    fpsTimer_.start();
}

void ViewportWidget::defaultCamera() {
    camera_.distance = 4.0f;
    camera_.yaw = glm::radians(45.0f);
    camera_.pitch = glm::radians(30.0f);
}

void ViewportWidget::setDocument(Document* document) {
    document_ = document;
    if (document_ == nullptr) {
        return;
    }
    connect(document_, &Document::characterChanged, this, [this] {
        characterDirty_ = true;
        focusSelection();
        update();
    });
    for (const auto signal : {&Document::poseChanged, &Document::selectionChanged,
                              &Document::skinMethodChanged, &Document::timeChanged}) {
        connect(document_, signal, this, qOverload<>(&QWidget::update));
    }
    connect(document_, &Document::toolChanged, this, [this] { updateCursor(); });
    connect(document_, &Document::visibilityChanged, this, qOverload<>(&QWidget::update));
    connect(document_, &Document::gizmoSpaceChanged, this, qOverload<>(&QWidget::update));
    connect(document_, &Document::hierarchyChanged, this, qOverload<>(&QWidget::update));
    connect(document_, &Document::meshDataChanged, this, [this] {
        characterDirty_ = true;
        update();
    });
    characterDirty_ = document_->hasCharacter();
    updateCursor();
}

void ViewportWidget::countFrame() { ++framesSinceReport_; }

ViewportWidget::~ViewportWidget() {
    if (context() != nullptr) {
        makeCurrent();
    }
}

void ViewportWidget::initializeGL() {
    const auto getProcAddress = [](const char* name) {
        const auto* context = QOpenGLContext::currentContext();
        if (context == nullptr) {
            return static_cast<void*>(nullptr);
        }
        return reinterpret_cast<void*>(context->getProcAddress(name));
    };
    if (!rigger::render::loadOpenGL(getProcAddress)) {
        qCritical() << "Failed to initialize OpenGL glad loader";
        return;
    }

    rigger::render::initializeViewportState();

    buildShaderProgram();
    buildGrid();
    buildSkinnedArm();
}

void ViewportWidget::resizeGL(int w, int h) {
    if (h == 0) {
        h = 1;
    }
    rigger::render::resizeViewport(w, h);
    lastWidth_ = w;
    lastHeight_ = h;
}

void ViewportWidget::paintGL() {
    RIGGER_PROFILE_SCOPE("paintGL");
    const auto frameStart = std::chrono::steady_clock::now();
    if (!queriesReady_) {
        glGenQueries(2, queries_);
        queriesReady_ = true;
    }
    glBeginQuery(GL_TIME_ELAPSED, queries_[queryIndex_]);
    struct EndFrame {  // clôt la requête GPU et lit celle de l'image précédente, quel que soit le chemin de
                       // sortie
        ViewportWidget* self;
        std::chrono::steady_clock::time_point start;
        ~EndFrame() {
            glEndQuery(GL_TIME_ELAPSED);
            self->queryPending_[self->queryIndex_] = true;
            self->queryIndex_ ^= 1;
            const int previous = self->queryIndex_;
            if (self->queryPending_[previous]) {
                GLint available = 0;
                glGetQueryObjectiv(self->queries_[previous], GL_QUERY_RESULT_AVAILABLE, &available);
                if (available != 0) {
                    GLuint64 nanoseconds = 0;
                    glGetQueryObjectui64v(self->queries_[previous], GL_QUERY_RESULT, &nanoseconds);
                    self->gpuMs_ = static_cast<double>(nanoseconds) / 1.0e6;
                    self->queryPending_[previous] = false;
                }
            }
            self->lastCpuMs_ =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    } endFrame{this, frameStart};
    countFrame();
    if (exportTransparent_) {
        GLfloat previous[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previous);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        rigger::render::clearViewport();
        glClearColor(previous[0], previous[1], previous[2], previous[3]);
    } else {
        rigger::render::clearViewport();
    }

    const float aspect =
        lastWidth_ > 0 ? static_cast<float>(lastWidth_) / static_cast<float>(lastHeight_) : 1.0f;
    const glm::mat4 view = camera_.viewMatrix();
    const glm::mat4 projection = camera_.projectionMatrix(aspect);
    const glm::mat4 viewProjection = projection * view;

    if (!shader_.isValid()) {
        return;
    }

    shader_.bind();
    shader_.setUniform("uViewProjection", viewProjection);
    shader_.setUniform("uSkinningEnabled", 0);
    shader_.setUniform("uShowInfluence", 0);
    shader_.setUniform("uSelectedBone", -1);

    if (characterDirty_) {
        rebuildCharacterRenderers();
    }

    if (!exportTransparent_ && !exportHideOverlays_) drawGrid();
    if (document_ != nullptr && document_->hasCharacter()) {
        drawCharacter();
    } else {
        drawSkinnedArm();
    }
    shader_.setUniform("uSkinningEnabled", 0);
    shader_.setUniform("uShowInfluence", 0);
    shader_.setUniform("uSelectedBone", -1);
    if (!exportHideOverlays_) {
        drawSkeleton();
        drawOverlays();
    }

    shader_.unbind();
}

// --- Souris / clavier -----------------------------------------------------------------------

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    lastPos_ = event->pos();
    pressPos_ = event->pos();
    leftPressed_ = false;
    rectSelecting_ = false;
    if (event->modifiers() & Qt::AltModifier) {
        orbiting_ = event->button() == Qt::LeftButton;
        panning_ = event->button() == Qt::MiddleButton || event->button() == Qt::RightButton;
    } else {
        orbiting_ = false;
        panning_ = false;
        if (event->button() == Qt::LeftButton && document_ != nullptr && document_->hasCharacter() &&
            skinPaintActive()) {
            if (document_->paintActive()) {
                if (document_->beginPaintStroke()) {
                    painting_ = true;
                    paintDab(event->pos());
                } else {
                    emit document_->warning(tr("Select the bone to paint in the hierarchy first"));
                }
            } else {
                leftPressed_ = true;  // sélection de sommets : clic, rectangle ou lasso (voir le relâchement)
                lasso_.assign(1, event->pos());
            }
        } else if (event->button() == Qt::LeftButton && document_ != nullptr && document_->hasCharacter()) {
            leftPressed_ = true;
            // Gizmo d'abord : un clic sur une poignée démarre un glissé au lieu de sélectionner.
            if (gizmoVisible() && updateGizmoFrame()) {
                const Ray ray = rayAt(event->pos());
                const auto handle = gizmo_.pick(ray);
                const glm::vec3 forward = glm::normalize(-glm::vec3(
                    camera_.viewMatrix()[0][2], camera_.viewMatrix()[1][2], camera_.viewMatrix()[2][2]));
                if (handle != rigger::render::GizmoHandle::None && document_->beginManipulation()) {
                    if (gizmo_.beginDrag(handle, ray, forward)) {
                        gizmoDragging_ = true;
                    } else {
                        document_->endManipulation();
                    }
                }
            }
        }
    }
    updateCursor();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - lastPos_;
    lastPos_ = event->pos();
    if (orbiting_) {
        camera_.orbit(-0.01f * delta.x(), 0.01f * delta.y());
        update();
    } else if (panning_) {
        camera_.pan(0.005f * delta.x(), -0.005f * delta.y());
        update();
    } else if (painting_) {
        paintDab(event->pos());
    } else if (gizmoDragging_) {
        const bool snap = (event->modifiers() & Qt::ControlModifier) != 0;
        document_->updateManipulation(gizmo_.drag(rayAt(event->pos()), snap));
    } else if (leftPressed_) {
        // Glissé sur le vide : sélection par rectangle (seuil anti-tremblement de 4 px).
        if (!rectSelecting_ && (event->pos() - pressPos_).manhattanLength() > 4) {
            rectSelecting_ = true;
        }
        if (rectSelecting_) {
            rectEnd_ = event->pos();
            if (skinPaintActive() && document_->vertexSelectMode() &&
                document_->vertexSelectShape() == VertexSelectShape::Lasso) {
                lasso_.push_back(event->pos());
            }
            update();
        }
    } else if (skinPaintActive() && document_ != nullptr && document_->paintActive()) {
        updateBrushPreview(event->pos());
    } else {
        updateHover(event->pos());
    }
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    const bool wasNavigating = orbiting_ || panning_;
    const bool clicked = (event->pos() - pressPos_).manhattanLength() < 4;
    orbiting_ = false;
    panning_ = false;

    if (event->button() == Qt::LeftButton && painting_) {
        painting_ = false;
        document_->endPaintStroke();
        update();
    }
    if (event->button() == Qt::LeftButton && leftPressed_) {
        leftPressed_ = false;
        if (skinPaintActive() && document_->vertexSelectMode()) {
            const bool dragged = rectSelecting_;
            rectSelecting_ = false;
            finishVertexSelection(event, !dragged ? SelectionKind::Click
                                         : document_->vertexSelectShape() == VertexSelectShape::Lasso
                                             ? SelectionKind::Lasso
                                             : SelectionKind::Rectangle);
            lasso_.clear();
        } else if (gizmoDragging_) {
            gizmoDragging_ = false;
            gizmo_.endDrag();
            document_->endManipulation();
        } else if (rectSelecting_) {
            rectSelecting_ = false;
            finishRectangle(event);
        } else if (clicked) {
            finishClick(event);
        }
        update();
    }
    updateCursor();
    // Clic droit simple (sans Alt, sans glissé) : menu contextuel.
    if (event->button() == Qt::RightButton && !wasNavigating && clicked && !contextActions_.isEmpty()) {
        QMenu menu(this);
        menu.addActions(contextActions_);
        menu.exec(event->globalPosition().toPoint());
    }
}

// --- Picking / sélection -------------------------------------------------------------------

glm::mat4 ViewportWidget::viewProjectionMatrix() const {
    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    return camera_.projectionMatrix(aspect) * camera_.viewMatrix();
}

Ray ViewportWidget::rayAt(const QPoint& position) const {
    return screenRay(viewProjectionMatrix(), static_cast<float>(position.x()),
                     static_cast<float>(position.y()), static_cast<float>(width()),
                     static_cast<float>(height()));
}

rigger::PickParams ViewportWidget::pickParams() const {
    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    const float p11 = camera_.projectionMatrix(aspect)[1][1];
    const float perPixel =
        2.0f / (p11 * static_cast<float>(std::max(height(), 1)));  // tan (persp) ou unités (ortho)
    rigger::PickParams params;
    params.cameraPosition = camera_.position();
    if (camera_.orthographic) {
        params.worldPerPixel = perPixel;
    } else {
        params.anglePerPixel = perPixel;
    }
    return params;
}

bool ViewportWidget::gizmoVisible() const {
    return document_ != nullptr && document_->hasCharacter() && document_->tool() != Tool::Select;
}

bool ViewportWidget::updateGizmoFrame() {
    if (gizmoDragging_) {
        return true;  // le repère reste figé pendant le glissé : le delta est calculé depuis son départ
    }
    glm::vec3 origin;
    glm::quat orientation;
    if (document_ == nullptr || !document_->gizmoFrame(origin, orientation)) {
        return false;
    }
    const rigger::PickParams params = pickParams();
    const float distance = glm::length(origin - params.cameraPosition);
    const float pixel = params.worldPerPixel + distance * params.anglePerPixel;
    gizmo_.setMode(document_->tool() == Tool::Move     ? rigger::render::GizmoMode::Translate
                   : document_->tool() == Tool::Rotate ? rigger::render::GizmoMode::Rotate
                                                       : rigger::render::GizmoMode::Scale);
    gizmo_.setFrame(origin, orientation, 100.0f * pixel);  // 100 px de rayon, quelle que soit la distance
    return true;
}

void ViewportWidget::updateHover(const QPoint& position) {
    if (document_ == nullptr || !document_->hasCharacter()) {
        return;
    }
    const Ray ray = rayAt(position);
    bool changed = false;
    auto handle = rigger::render::GizmoHandle::None;
    if (gizmoVisible() && updateGizmoFrame()) {
        handle = gizmo_.pick(ray);
    }
    if (handle != gizmo_.hover()) {
        gizmo_.setHover(handle);
        changed = true;
    }
    BoneId bone = kNoBone;
    if (handle == rigger::render::GizmoHandle::None) {
        const auto globals = computeGlobals(document_->character()->skeleton, document_->pose());
        const auto hits = pickBones(document_->character()->skeleton, globals, ray, pickParams(),
                                    [this](BoneId id) { return document_->isBonePickable(id); });
        bone = hits.empty() ? kNoBone : hits.front().id;
    }
    if (bone != hoverBone_) {
        hoverBone_ = bone;
        changed = true;
    }
    if (changed) {
        update();
    }
}

void ViewportWidget::finishClick(const QMouseEvent* event) {
    const auto character = document_->character();
    const auto globals = computeGlobals(character->skeleton, document_->pose());
    const auto hits = pickBones(character->skeleton, globals, rayAt(event->pos()), pickParams(),
                                [this](BoneId id) { return document_->isBonePickable(id); });
    // Clic répété au même endroit sur les mêmes candidats : os suivant (os superposés).
    const auto sameHits = [](const std::vector<PickHit>& a, const std::vector<PickHit>& b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(),
                          [](const PickHit& x, const PickHit& y) { return x.id == y.id; });
    };
    BoneId picked = kNoBone;
    if (!hits.empty()) {
        const bool repeated =
            (event->pos() - lastClickPos_).manhattanLength() <= 3 && sameHits(hits, lastClickHits_);
        const BoneId current = document_->selection().size() == 1 ? document_->selection().front() : kNoBone;
        picked = repeated ? cyclePick(hits, current) : hits.front().id;
    }
    lastClickPos_ = event->pos();
    lastClickHits_ = hits;

    const Qt::KeyboardModifiers mods = event->modifiers();
    if (picked == kNoBone) {
        if (!(mods & (Qt::ControlModifier | Qt::ShiftModifier))) {
            document_->clearSelection();  // clic dans le vide
        }
    } else if (mods & Qt::ControlModifier) {
        document_->toggleInSelection(picked);
    } else if (mods & Qt::ShiftModifier) {
        document_->selectRangeTo(picked);
    } else {
        document_->setSelection({picked});
    }
}

bool ViewportWidget::skinPaintActive() const {
    return document_ != nullptr && document_->mode() == Mode::Skin &&
           (document_->paintActive() || document_->vertexSelectMode());
}

void ViewportWidget::paintDab(const QPoint& position) {
    const auto hit = document_->raycastSurface(rayAt(position));
    brushValid_ = hit.has_value();
    if (hit) {
        brushPoint_ = *hit;
        document_->paintAt(*hit, camera_.position());
    }
    update();
}

void ViewportWidget::updateBrushPreview(const QPoint& position) {
    const auto hit = document_->raycastSurface(rayAt(position));
    brushValid_ = hit.has_value();
    if (hit) brushPoint_ = *hit;
    update();
}

void ViewportWidget::finishVertexSelection(const QMouseEvent* event, SelectionKind kind) {
    const bool rectangle = kind != SelectionKind::Click;
    std::vector<glm::vec2> polygon;
    if (kind == SelectionKind::Lasso) {
        for (const QPoint& p : lasso_)
            polygon.emplace_back(static_cast<float>(p.x()), static_cast<float>(p.y()));
    }
    const glm::mat4 viewProjection = viewProjectionMatrix();
    const auto toPixel = [&](const glm::vec3& p, bool& inFront) {
        const glm::vec4 clip = viewProjection * glm::vec4(p, 1.0f);
        inFront = clip.w > 1e-6f;
        const glm::vec2 ndc = inFront ? glm::vec2(clip.x, clip.y) / clip.w : glm::vec2(0.0f);
        return glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(width()),
                         (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height()));
    };
    std::vector<Document::VertexRef> picked;
    const auto character = document_->character();
    float bestDistance = 12.0f;
    Document::VertexRef nearest;
    bool haveNearest = false;
    const QRect box = QRect(pressPos_, event->pos()).normalized();
    for (std::size_t m = 0; m < character->meshes.size(); ++m) {
        const auto verts = document_->skinnedVertices(m);
        for (std::size_t v = 0; v < verts.size(); ++v) {
            bool front = false;
            const glm::vec2 px = toPixel(verts[v].position, front);
            if (!front) continue;
            if (rectangle) {
                const bool inside = kind == SelectionKind::Lasso
                                        ? polygon.size() >= 3 && pointInPolygon(px, polygon)
                                        : box.contains(static_cast<int>(px.x), static_cast<int>(px.y));
                if (inside) picked.push_back({m, static_cast<std::uint32_t>(v)});
            } else {
                const float d = glm::length(px - glm::vec2(static_cast<float>(event->pos().x()),
                                                           static_cast<float>(event->pos().y())));
                if (d < bestDistance) {
                    bestDistance = d;
                    nearest = {m, static_cast<std::uint32_t>(v)};
                    haveNearest = true;
                }
            }
        }
    }
    if (!rectangle && haveNearest) picked.push_back(nearest);
    if (event->modifiers() & Qt::ControlModifier) {
        for (const auto& ref : document_->vertexSelection()) {
            if (std::find(picked.begin(), picked.end(), ref) == picked.end()) picked.push_back(ref);
        }
    }
    document_->setVertexSelection(std::move(picked));
}

void ViewportWidget::finishRectangle(const QMouseEvent* event) {
    const auto toNdc = [this](const QPoint& p) {
        return glm::vec2(2.0f * static_cast<float>(p.x()) / static_cast<float>(std::max(width(), 1)) - 1.0f,
                         1.0f - 2.0f * static_cast<float>(p.y()) / static_cast<float>(std::max(height(), 1)));
    };
    const auto character = document_->character();
    const auto globals = computeGlobals(character->skeleton, document_->pose());
    std::vector<BoneId> ids =
        bonesInRect(character->skeleton, globals, viewProjectionMatrix(), toNdc(pressPos_),
                    toNdc(event->pos()), [this](BoneId id) { return document_->isBonePickable(id); });
    if (event->modifiers() & Qt::ControlModifier) {
        for (const BoneId id : document_->selection()) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
                ids.push_back(id);
            }
        }
    }
    document_->setSelection(std::move(ids));
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    camera_.zoom(static_cast<float>(event->angleDelta().y()) * 0.001f);
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (document_ != nullptr && document_->mode() == Mode::Skin &&
        (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight)) {
        Brush brush = document_->brush();  // [ / ] : rayon du pinceau
        brush.radius *= event->key() == Qt::Key_BracketLeft ? 0.85f : 1.0f / 0.85f;
        document_->setBrush(brush);
        update();
        return;
    }
    if (event->key() == Qt::Key_Alt) {
        altDown_ = true;
        updateCursor();
    }
    QOpenGLWidget::keyPressEvent(event);
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Alt) {
        altDown_ = false;
        updateCursor();
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

// Le viewport prend le focus clavier au survol, sauf si l'utilisateur est en train de saisir du texte
// ailleurs.
void ViewportWidget::enterEvent(QEnterEvent* event) {
    QWidget* focused = QApplication::focusWidget();
    const bool typing = qobject_cast<QLineEdit*>(focused) != nullptr ||
                        qobject_cast<QAbstractSpinBox*>(focused) != nullptr ||
                        qobject_cast<QPlainTextEdit*>(focused) != nullptr;
    if (window() != nullptr && window()->isActiveWindow() && !typing) {
        setFocus(Qt::MouseFocusReason);
    }
    QOpenGLWidget::enterEvent(event);
}

void ViewportWidget::updateCursor() {
    if (orbiting_) {
        setCursor(Qt::ClosedHandCursor);
    } else if (panning_) {
        setCursor(Qt::SizeAllCursor);
    } else if (altDown_) {
        setCursor(Qt::OpenHandCursor);
    } else if (document_ == nullptr) {
        unsetCursor();
    } else {
        switch (document_->tool()) {
            case Tool::Select:
                setCursor(Qt::ArrowCursor);
                break;
            case Tool::Move:
                setCursor(Qt::SizeAllCursor);
                break;
            case Tool::Rotate:
                setCursor(Qt::CrossCursor);
                break;
            case Tool::Scale:
                setCursor(Qt::SizeFDiagCursor);
                break;
        }
    }
}

// --- Slots ----------------------------------------------------------------------------------

void ViewportWidget::resetCamera() {
    defaultCamera();
    camera_.orthographic = false;
    focusSelection();
}

void ViewportWidget::setOrthographic(bool enabled) {
    camera_.orthographic = enabled;
    update();
}

void ViewportWidget::setGpuSkinning(bool enabled) {
    gpuSkinning_ = enabled;
    update();
}

void ViewportWidget::setShowInfluence(bool enabled) {
    showInfluence_ = enabled;
    update();
}

void ViewportWidget::focusSelection() {
    const auto defaultFocus = [this] { camera_.focusOn(glm::vec3(0.0f, 0.5f, 0.0f), 1.6f); };
    if (document_ == nullptr || !document_->hasCharacter()) {
        defaultFocus();
        update();
        return;
    }
    const auto character = document_->character();
    // Sélection : cadre les origines des os sélectionnés ; sinon le maillage au repos (repère de bind).
    if (!document_->selection().empty()) {
        const auto globals = rigger::computeGlobals(character->skeleton, document_->pose());
        glm::vec3 lo(0.0f);
        glm::vec3 hi(0.0f);
        bool first = true;
        for (const rigger::BoneId id : document_->selection()) {
            const glm::vec3 p(globals[id][3]);
            lo = first ? p : glm::min(lo, p);
            hi = first ? p : glm::max(hi, p);
            first = false;
        }
        camera_.focusOn((lo + hi) * 0.5f, std::max(glm::length(hi - lo) * 0.5f, 0.5f));
        update();
        return;
    }
    bool any = false;
    glm::vec3 lo(0.0f);
    glm::vec3 hi(0.0f);
    for (const rigger::Mesh& mesh : character->meshes) {
        const rigger::Bounds b = mesh.computeBounds();
        lo = any ? glm::min(lo, b.min) : b.min;
        hi = any ? glm::max(hi, b.max) : b.max;
        any = true;
    }
    if (any) {
        camera_.focusOn((lo + hi) * 0.5f, std::max(glm::length(hi - lo) * 0.5f, 0.1f));
    } else {
        defaultFocus();
    }
    update();
}

void ViewportWidget::buildShaderProgram() {
    if (!shader_.compileFromSource(kVertexShaderSource, kFragmentShaderSource)) {
        qCritical() << "Failed to compile viewport shader";
    }
}

void ViewportWidget::buildGrid() { grid_.rebuild(20.0f, 40); }

void ViewportWidget::buildSkinnedArm() {
    const auto upperArm = demoSkeleton_.addBone("upper_arm", rigger::kNoBone);
    if (!upperArm) {
        qCritical() << "Failed to create the demo upper-arm bone:"
                    << QString::fromStdString(upperArm.error().message);
        return;
    }
    rigger::Transform elbowLocal;
    elbowLocal.t = {0.0f, 1.0f, 0.0f};
    const auto forearm = demoSkeleton_.addBone("forearm", upperArm.value(), elbowLocal);
    if (!forearm) {
        qCritical() << "Failed to create the demo forearm bone:"
                    << QString::fromStdString(forearm.error().message);
        return;
    }
    forearmBone_ = forearm.value();
    demoPose_ = demoSkeleton_.bindPose();
    demoPose_[forearmBone_].r = glm::angleAxis(glm::radians(-65.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    std::vector<rigger::render::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    constexpr std::uint32_t kRadialSegments = 20;
    constexpr std::uint32_t kAxialSegments = 40;
    constexpr float kRadius = 0.16f;
    constexpr float kElbowBlendStart = 0.65f;
    constexpr float kElbowBlendEnd = 1.35f;
    vertices.reserve((kAxialSegments + 1) * kRadialSegments);
    indices.reserve(kAxialSegments * kRadialSegments * 6);
    for (std::uint32_t row = 0; row <= kAxialSegments; ++row) {
        const float y = 2.0f * static_cast<float>(row) / static_cast<float>(kAxialSegments);
        const float blend =
            glm::clamp((y - kElbowBlendStart) / (kElbowBlendEnd - kElbowBlendStart), 0.0f, 1.0f);
        const float forearmWeight = blend * blend * (3.0f - 2.0f * blend);
        const glm::vec3 color =
            glm::mix(glm::vec3(0.18f, 0.55f, 0.95f), glm::vec3(0.95f, 0.55f, 0.18f), forearmWeight);
        for (std::uint32_t segment = 0; segment < kRadialSegments; ++segment) {
            const float angle =
                glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(kRadialSegments);
            const glm::vec3 normal(std::cos(angle), 0.0f, std::sin(angle));
            rigger::render::MeshVertex vertex;
            vertex.position = normal * kRadius + glm::vec3(0.0f, y, 0.0f);
            vertex.normal = normal;
            vertex.color = color;
            vertex.joints = glm::uvec4(upperArm.value(), forearmBone_, 0u, 0u);
            vertex.weights = glm::vec4(1.0f - forearmWeight, forearmWeight, 0.0f, 0.0f);
            vertices.push_back(vertex);
        }
    }
    for (std::uint32_t row = 0; row < kAxialSegments; ++row) {
        const std::uint32_t base = row * kRadialSegments;
        for (std::uint32_t segment = 0; segment < kRadialSegments; ++segment) {
            const std::uint32_t next = (segment + 1) % kRadialSegments;
            const std::uint32_t lower = base + segment;
            const std::uint32_t lowerNext = base + next;
            const std::uint32_t upper = base + kRadialSegments + segment;
            const std::uint32_t upperNext = base + kRadialSegments + next;
            indices.insert(indices.end(), {lower, lowerNext, upperNext, lower, upperNext, upper});
        }
    }
    armRenderer_.setMesh(vertices, indices);
    if (!armRenderer_.setBoneMatrices(rigger::computeSkinMatrices(demoSkeleton_, demoPose_))) {
        qCritical() << "Failed to configure the demo arm skinning palette";
    }
}

void ViewportWidget::drawGrid() { grid_.draw(); }

void ViewportWidget::drawSkinnedArm() {
    if (!armRenderer_.setBoneMatrices(rigger::computeSkinMatrices(demoSkeleton_, demoPose_))) {
        qCritical() << "Failed to update the demo arm skinning palette";
        return;
    }
    armRenderer_.draw(shader_, gpuSkinning_, showInfluence_ ? forearmBone_ : rigger::kNoBone);
}

void ViewportWidget::drawSkeleton() {
    RIGGER_PROFILE_SCOPE("drawSkeleton");
    std::vector<rigger::render::SkeletonRenderer::ColoredSegment> segments;
    if (document_ != nullptr && document_->hasCharacter()) {
        const glm::vec3 kSelected(0.3f, 0.85f,
                                  1.0f);  // cyan : l'orange / jaune sont réservés à l'état des clés
        const glm::vec3 kHover(0.85f, 0.97f, 1.0f);
        const glm::vec3 kKeyed(1.0f, 0.9f, 0.1f);     // clé présente à la frame courante
        const glm::vec3 kModified(1.0f, 0.5f, 0.1f);  // modifié depuis la dernière clé
        const auto character = document_->character();
        const rigger::Skeleton& skeleton = character->skeleton;
        const auto globals = rigger::computeGlobals(skeleton, document_->pose());
        const rigger::PickParams params = pickParams();
        const bool animate = document_->mode() == Mode::Animate && document_->clip() != nullptr;
        const auto keyStates = animate ? document_->boneKeyStates() : std::vector<BoneKeyState>{};
        // Pelure d'oignon : squelettes fantômes aux images voisines (bleu avant, vert après).
        const OnionSkin& onion = document_->onionSkin();
        if (animate && onion.enabled) {
            const rigger::AnimationClip& clip = *document_->clip();
            const double fps = document_->fps();
            for (int k = -onion.before; k <= onion.after; ++k) {
                const double t = document_->time() + k * onion.step / fps;
                if (k == 0 || t < 0.0 || t > clip.duration + 1e-9) {
                    continue;
                }
                const float fade = 1.0f - 0.6f * static_cast<float>(std::abs(k)) /
                                              static_cast<float>(std::max(onion.before, onion.after) + 1);
                const glm::vec3 ghost =
                    (k < 0 ? glm::vec3(0.25f, 0.45f, 1.0f) : glm::vec3(0.3f, 0.9f, 0.4f)) * fade;
                const rigger::Pose ghostPose = rigger::samplePose(clip, skeleton, t);
                const auto ghostGlobals = rigger::computeGlobals(skeleton, ghostPose);
                for (const rigger::BoneId id : skeleton.order()) {
                    if (!document_->isBoneVisible(id)) {
                        continue;
                    }
                    const rigger::BoneSegments seg = rigger::boneSegments(skeleton, ghostGlobals, id);
                    if (seg.hasLink) segments.push_back({seg.linkFrom, seg.linkTo, ghost});
                    if (seg.hasStub) segments.push_back({seg.origin, seg.stubTo, ghost});
                }
            }
        }
        for (const rigger::BoneId id : skeleton.order()) {
            if (!document_->isBoneVisible(id) ||
                rigger::hasFlag(skeleton.bone(id).flags, rigger::BoneFlags::Morph)) {
                continue;
            }
            const rigger::Bone& bone = skeleton.bone(id);
            const glm::vec3 color =
                document_->isSelected(id) ? kSelected : (id == hoverBone_ ? kHover : bone.color);
            glm::vec3 crossColor = color;
            if (animate && id < keyStates.size() && keyStates[id] != BoneKeyState::None) {
                crossColor = keyStates[id] == BoneKeyState::Keyed ? kKeyed : kModified;
            }
            const rigger::BoneSegments seg = rigger::boneSegments(skeleton, globals, id);
            if (rigger::hasFlag(bone.flags, rigger::BoneFlags::Control)) {
                // Os de contrôle IK (cible / pole) : octaèdre de taille constante à l'écran (9 px), pas de
                // lien.
                const float r =
                    9.0f * (params.worldPerPixel +
                            glm::length(seg.origin - params.cameraPosition) * params.anglePerPixel);
                const glm::vec3 ring[4] = {{r, 0, 0}, {0, 0, r}, {-r, 0, 0}, {0, 0, -r}};
                for (int k = 0; k < 4; ++k) {
                    segments.push_back({seg.origin + glm::vec3(0, r, 0), seg.origin + ring[k], crossColor});
                    segments.push_back({seg.origin - glm::vec3(0, r, 0), seg.origin + ring[k], crossColor});
                    segments.push_back({seg.origin + ring[k], seg.origin + ring[(k + 1) % 4], crossColor});
                }
                continue;
            }
            if (seg.hasLink) {
                segments.push_back({seg.linkFrom, seg.linkTo, color});
            }
            if (seg.hasStub) {
                segments.push_back({seg.origin, seg.stubTo, color});
            }
            // Croix d'articulation de taille constante à l'écran (4 px).
            const float s = 4.0f * (params.worldPerPixel +
                                    glm::length(seg.origin - params.cameraPosition) * params.anglePerPixel);
            for (int axis = 0; axis < 3; ++axis) {
                glm::vec3 d(0.0f);
                d[axis] = s;
                segments.push_back({seg.origin - d, seg.origin + d, crossColor});
            }
        }
        // Lignes pointillées effecteur -> cible et coude -> pole des contrôles IK actifs.
        if (animate) {
            const auto dotted = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) {
                const float length = glm::length(b - a);
                const int dashes = std::clamp(static_cast<int>(length / 0.04f), 1, 200);
                for (int k = 0; k < dashes; k += 2) {
                    segments.push_back(
                        {glm::mix(a, b, static_cast<float>(k) / static_cast<float>(dashes)),
                         glm::mix(a, b, static_cast<float>(k + 1) / static_cast<float>(dashes)), color});
                }
            };
            for (const rigger::IKControl& c : document_->ikControls()) {
                if (!c.enabled || !skeleton.contains(c.targetBone) || c.chain.bones.empty()) continue;
                const rigger::BoneId end =
                    c.kind == rigger::IKKind::LookAt ? c.chain.bones[0] : c.chain.effector;
                if (!skeleton.contains(end)) continue;
                dotted(glm::vec3(globals[end][3]), glm::vec3(globals[c.targetBone][3]),
                       glm::vec3(1.0f, 0.3f, 0.9f));
                if (skeleton.contains(c.poleBone) && c.chain.bones.size() >= 2) {
                    dotted(glm::vec3(globals[c.chain.bones.back()][3]), glm::vec3(globals[c.poleBone][3]),
                           glm::vec3(0.3f, 1.0f, 0.5f));
                }
            }
        }
    } else {
        const glm::vec3 color(0.1f, 0.7f, 1.0f);
        const auto globals = rigger::computeGlobals(demoSkeleton_, demoPose_);
        const glm::vec3 shoulder = glm::vec3(globals[0][3]);
        const glm::vec3 elbow = glm::vec3(globals[forearmBone_][3]);
        const glm::vec3 wrist = glm::vec3(globals[forearmBone_] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
        segments = {{shoulder, elbow, color}, {elbow, wrist, color}};
    }
    // Os visibles « aux rayons X » : dessinés par-dessus le maillage.
    glDisable(GL_DEPTH_TEST);
    skeletonRenderer_.setColoredSegments(segments);
    skeletonRenderer_.draw();
    glEnable(GL_DEPTH_TEST);
}

void ViewportWidget::drawOverlays() {
    glDisable(GL_DEPTH_TEST);
    if (document_ != nullptr && document_->hasCharacter() && document_->mode() == Mode::Skin) {
        std::vector<rigger::render::SkeletonRenderer::ColoredSegment> marks;
        if (document_->paintActive() && brushValid_) {
            const glm::mat4 view = camera_.viewMatrix();
            const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
            const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
            const float radius = document_->brush().radius;
            const glm::vec3 color = document_->brush().mode == BrushMode::Subtract
                                        ? glm::vec3(1.0f, 0.3f, 0.3f)
                                        : glm::vec3(1.0f);
            constexpr int kSteps = 48;
            for (int i = 0; i < kSteps; ++i) {
                const float a0 = glm::two_pi<float>() * static_cast<float>(i) / kSteps;
                const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / kSteps;
                marks.push_back({brushPoint_ + (right * std::cos(a0) + up * std::sin(a0)) * radius,
                                 brushPoint_ + (right * std::cos(a1) + up * std::sin(a1)) * radius, color});
            }
        }
        if (leftPressed_ && lasso_.size() > 1 && document_->vertexSelectMode()) {
            // Tracé du lasso : points ramenés à 1 m devant la caméra.
            for (std::size_t i = 1; i < lasso_.size(); ++i) {
                marks.push_back(
                    {rayAt(lasso_[i - 1]).at(1.0f), rayAt(lasso_[i]).at(1.0f), glm::vec3(1.0f, 0.9f, 0.1f)});
            }
            marks.push_back(
                {rayAt(lasso_.back()).at(1.0f), rayAt(lasso_.front()).at(1.0f), glm::vec3(1.0f, 0.9f, 0.1f)});
        }
        const rigger::PickParams params = pickParams();
        for (const auto& ref : document_->vertexSelection()) {
            const auto verts = document_->skinnedVertices(ref.mesh);
            if (ref.vertex >= verts.size()) continue;
            const glm::vec3 p = verts[ref.vertex].position;
            const float s =
                4.0f * (params.worldPerPixel + glm::length(p - params.cameraPosition) * params.anglePerPixel);
            for (int axis = 0; axis < 3; ++axis) {
                glm::vec3 d(0.0f);
                d[axis] = s;
                marks.push_back({p - d, p + d, glm::vec3(1.0f, 0.9f, 0.1f)});
            }
        }
        if (!marks.empty()) {
            skeletonRenderer_.setColoredSegments(marks);
            skeletonRenderer_.draw();
        }
    }
    if (gizmoVisible() && updateGizmoFrame()) {
        const rigger::render::GizmoMesh mesh = gizmo_.buildMesh();
        std::vector<rigger::render::MeshVertex> vertices(mesh.positions.size());
        const glm::vec3 lit =
            glm::normalize(glm::vec3(0.8f, 1.0f, 0.6f));  // normale = lumière : couleur pleine
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            vertices[i].position = mesh.positions[i];
            vertices[i].normal = lit;
            vertices[i].color = mesh.colors[i];
            vertices[i].weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
        shader_.setUniform("uSkinningEnabled", 0);
        shader_.setUniform("uShowInfluence", 0);
        shader_.setUniform("uSelectedBone", -1);
        gizmoRenderer_.setMesh(vertices, mesh.indices);
        gizmoRenderer_.draw();
    }
    if (rectSelecting_) {
        // Rectangle de sélection tracé en coordonnées normalisées de l'écran.
        const auto toNdc = [this](const QPoint& p) {
            return glm::vec3(
                2.0f * static_cast<float>(p.x()) / static_cast<float>(std::max(width(), 1)) - 1.0f,
                1.0f - 2.0f * static_cast<float>(p.y()) / static_cast<float>(std::max(height(), 1)), 0.0f);
        };
        const glm::vec3 a = toNdc(pressPos_), c = toNdc(rectEnd_);
        const glm::vec3 b(c.x, a.y, 0.0f), d(a.x, c.y, 0.0f);
        const glm::vec3 color(1.0f, 0.9f, 0.3f);
        rectRenderer_.setColoredSegments({{a, b, color}, {b, c, color}, {c, d, color}, {d, a, color}});
        shader_.setUniform("uViewProjection", glm::mat4(1.0f));
        rectRenderer_.draw();
    }
    glEnable(GL_DEPTH_TEST);
}

void ViewportWidget::uploadMesh(std::size_t index, const std::vector<float>& weights) {
    RIGGER_PROFILE_SCOPE("uploadMesh");
    const auto character = document_->character();
    const rigger::Mesh& mesh = character->meshes[index];
    const std::vector<rigger::Vertex> rest =
        rigger::anyMorphActive(weights) ? rigger::applyMorphs(mesh, weights) : mesh.vertices;
    std::vector<rigger::render::MeshVertex> vertices(rest.size());
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        vertices[v].position = rest[v].position;
        vertices[v].normal = rest[v].normal;
        vertices[v].color = glm::vec3(0.8f);
        if (mesh.hasSkinning()) {
            const rigger::SkinWeight& skin = mesh.skinWeights[v];
            for (std::size_t k = 0; k < 4; ++k) {
                // Les emplacements vides (kNoBone, poids 0) pointent sur l'os 0 : sans effet.
                vertices[v].joints[static_cast<int>(k)] =
                    skin.joints[k] == rigger::kNoBone ? 0u : skin.joints[k];
                vertices[v].weights[static_cast<int>(k)] = skin.weights[k];
            }
        } else {
            vertices[v].weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    // Couleur par sous-maillage = couleur de base du matériau (textures : phases ultérieures).
    for (const rigger::Submesh& submesh : mesh.submeshes) {
        for (std::uint32_t i = submesh.firstIndex; i < submesh.endIndex(); ++i) {
            vertices[mesh.indices[i]].color = submesh.material.baseColor;
        }
    }
    characterMeshes_[index].setMesh(vertices, mesh.indices);
    morphCache_[index] = weights;
}

void ViewportWidget::rebuildCharacterRenderers() {
    characterDirty_ = false;
    characterMeshes_.clear();
    characterMeshSkinned_.clear();
    morphCache_.clear();
    if (document_ == nullptr || !document_->hasCharacter()) {
        return;
    }
    const auto character = document_->character();
    characterMeshes_.reserve(character->meshes.size());
    for (std::size_t i = 0; i < character->meshes.size(); ++i) {
        characterMeshes_.emplace_back();
        morphCache_.emplace_back();
        characterMeshSkinned_.push_back(character->meshes[i].hasSkinning() ? 1 : 0);
        uploadMesh(i, rigger::morphWeights(character->meshes[i], character->skeleton, document_->pose()));
    }
}

void ViewportWidget::drawCharacter() {
    RIGGER_PROFILE_SCOPE("drawCharacter");
    const auto character = document_->character();
    const auto skinMatrices = rigger::computeSkinMatrices(character->skeleton, document_->pose());
    const bool dqs = document_->skinMethod() == SkinMethod::Dqs;
    // Bone dont on affiche l'influence : le premier os sélectionné.
    std::uint32_t influenceBone =
        showInfluence_ && !document_->selection().empty() ? document_->selection().front() : rigger::kNoBone;
    if (document_->mode() == Mode::Skin && document_->activeBone() != rigger::kNoBone) {
        influenceBone = document_->activeBone();  // mode Skin : heatmap de l'os actif
    }
    for (std::size_t i = 0; i < characterMeshes_.size(); ++i) {
        if (!character->meshes[i]
                 .morphTargets.empty()) {  // blend shapes : re-téléverse si les poids ont changé
            const auto weights =
                rigger::morphWeights(character->meshes[i], character->skeleton, document_->pose());
            if (weights != morphCache_[i]) uploadMesh(i, weights);
        }
        if (characterMeshSkinned_[i] == 0) {
            characterMeshes_[i].draw(shader_, true, rigger::kNoBone);  // palette identité : maillage statique
            continue;
        }
        if (!characterMeshes_[i].setBoneMatrices(skinMatrices)) {
            qCritical() << "Skinning palette rejected (more than 256 bones or invalid matrices)";
            continue;
        }
        characterMeshes_[i].setDualQuaternionSkinning(dqs);
        // Le DQS n'existe qu'en CPU (la palette GPU est linéaire).
        characterMeshes_[i].draw(shader_, gpuSkinning_ && !dqs, influenceBone);
    }
}

}  // namespace rigger::editor
