// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/TimelineView.hpp"

#include <QAction>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <tuple>

#include "editor/Document.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

namespace {

using SelKey = std::tuple<BoneId, int, long long>;
long long quantize(double t) { return std::llround(t * 10000.0); }

const Track<glm::vec3>* vecTrack(const BoneTracks& tracks, int channel) {
    return channel == ChannelScale ? &tracks.scale
                                   : (channel == ChannelTranslation ? &tracks.translation : nullptr);
}

// Appelle fn(time) pour chaque clé de la piste dans [t0, t1] (recherche binaire du début).
template <class Fn>
void forKeysInRange(const BoneTracks& tracks, int channel, double t0, double t1, Fn&& fn) {
    const auto run = [&](const auto& track) {
        auto it = std::lower_bound(track.keys.begin(), track.keys.end(), t0,
                                   [](const auto& k, double t) { return k.time < t; });
        for (; it != track.keys.end() && it->time <= t1; ++it) {
            fn(it->time);
        }
    };
    if (channel == ChannelRotation) {
        run(tracks.rotation);
    } else {
        run(*vecTrack(tracks, channel));
    }
}

}  // namespace

TimelineView::TimelineView(Document* document, QWidget* parent)
    : QWidget(parent),
      document_(document),
      hbar_(new QScrollBar(Qt::Horizontal, this)),
      vbar_(new QScrollBar(Qt::Vertical, this)) {
    setObjectName("timelineView");
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(120);
    hbar_->setObjectName("timelineHBar");
    vbar_->setObjectName("timelineVBar");
    connect(hbar_, &QScrollBar::valueChanged, this, [this](int v) {
        if (!updatingBars_) {
            firstFrame_ = v;
            update();
            emit viewChanged();
        }
    });
    connect(vbar_, &QScrollBar::valueChanged, this, [this](int v) {
        if (!updatingBars_) {
            scrollRow_ = v;
            update();
        }
    });
    connect(document_, &Document::characterChanged, this, [this] {
        expanded_.clear();
        collapsed_.clear();
        rebuildRows();
        fitToClip();
    });
    connect(document_, &Document::hierarchyChanged, this, &TimelineView::rebuildRows);
    connect(document_, &Document::animationChanged, this, [this] {
        updateScrollBars();
        update();
    });
    connect(document_, &Document::clipChanged, this, [this] {
        fitToClip();
        update();
    });
    for (auto signal : {&Document::keySelectionChanged, &Document::timeChanged, &Document::selectionChanged,
                        &Document::poseChanged}) {
        connect(document_, signal, this, qOverload<>(&QWidget::update));
    }
    rebuildRows();
}

double TimelineView::fps() const { return document_->fps(); }

int TimelineView::xForTime(double seconds) const {
    return kLabelWidth + static_cast<int>(std::lround((seconds * fps() - firstFrame_) * pixelsPerFrame_));
}

double TimelineView::timeForX(int x) const {
    return ((x - kLabelWidth) / pixelsPerFrame_ + firstFrame_) / fps();
}

int TimelineView::frameForX(int x) const {
    return static_cast<int>(std::lround((x - kLabelWidth) / pixelsPerFrame_ + firstFrame_));
}

int TimelineView::visibleRowCount() const {
    return std::max(0, (height() - kRulerHeight - kScrollBarSize) / kRowHeight);
}

QRect TimelineView::rowRect(int rowIndex) const {
    return QRect(0, kRulerHeight + (rowIndex - scrollRow_) * kRowHeight, width() - kScrollBarSize,
                 kRowHeight);
}

int TimelineView::rowAt(int y) const {
    if (y < kRulerHeight) {
        return -1;
    }
    const int index = scrollRow_ + (y - kRulerHeight) / kRowHeight;
    return index >= 0 && index < static_cast<int>(rows_.size()) ? index : -1;
}

void TimelineView::setView(double firstFrame, double pixelsPerFrame) {
    pixelsPerFrame_ = std::clamp(pixelsPerFrame, 1.0, 200.0);
    firstFrame_ = firstFrame;
    clampView();
    updateScrollBars();
    update();
    emit viewChanged();
}

void TimelineView::clampView() { firstFrame_ = std::max(firstFrame_, -5.0); }

void TimelineView::fitToClip() {
    const AnimationClip* clip = document_->clip();
    const double frames = clip != nullptr ? std::max(clip->duration * fps(), 10.0) : 60.0;
    const double canvas = std::max(width() - kLabelWidth - kScrollBarSize - 24, 100);
    setView(0.0, std::clamp(canvas / frames, 2.0, 60.0));
}

void TimelineView::setBoneExpanded(BoneId bone, bool expanded) {
    if (expanded) {
        expanded_.insert(bone);
    } else {
        expanded_.erase(bone);
    }
    rebuildRows();
}

void TimelineView::setBranchCollapsed(BoneId bone, bool collapsed) {
    if (collapsed) {
        collapsed_.insert(bone);
    } else {
        collapsed_.erase(bone);
    }
    rebuildRows();
}

void TimelineView::rebuildRows() {
    rows_.clear();
    const Skeleton* skeleton = document_->skeleton();
    if (skeleton != nullptr) {
        for (const BoneId id : skeleton->order()) {
            int depth = 0;
            bool hidden = false;
            for (BoneId p = skeleton->bone(id).parent; p != kNoBone; p = skeleton->bone(p).parent) {
                ++depth;
                hidden = hidden || collapsed_.count(p) != 0;
            }
            if (hidden) {
                continue;
            }
            rows_.push_back({id, -1, depth});
            if (expanded_.count(id) != 0) {
                for (int c = 0; c < 3; ++c) {
                    rows_.push_back({id, c, depth + 1});
                }
            }
        }
    }
    updateScrollBars();
    update();
}

void TimelineView::updateScrollBars() {
    updatingBars_ = true;
    const AnimationClip* clip = document_->clip();
    const double totalFrames = (clip != nullptr ? clip->duration * fps() : 60.0) + 60.0;
    const double visibleFrames = std::max(width() - kLabelWidth - kScrollBarSize, 1) / pixelsPerFrame_;
    hbar_->setRange(0, std::max(0, static_cast<int>(std::ceil(totalFrames - visibleFrames))));
    hbar_->setPageStep(std::max(1, static_cast<int>(visibleFrames)));
    hbar_->setValue(static_cast<int>(std::max(firstFrame_, 0.0)));
    const int hidden = std::max(0, static_cast<int>(rows_.size()) - visibleRowCount());
    vbar_->setRange(0, hidden);
    vbar_->setPageStep(std::max(1, visibleRowCount()));
    scrollRow_ = std::clamp(scrollRow_, 0, hidden);
    vbar_->setValue(scrollRow_);
    updatingBars_ = false;
}

void TimelineView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    hbar_->setGeometry(kLabelWidth, height() - kScrollBarSize, width() - kLabelWidth - kScrollBarSize,
                       kScrollBarSize);
    vbar_->setGeometry(width() - kScrollBarSize, kRulerHeight, kScrollBarSize,
                       height() - kRulerHeight - kScrollBarSize);
    updateScrollBars();
}

// --- Sélection de clés ----------------------------------------------------------------------------

bool TimelineView::isSelected(const KeyRef& key) const {
    const auto& sel = document_->keySelection();
    return std::any_of(sel.begin(), sel.end(), [&](const KeyRef& k) { return k == key; });
}

std::vector<KeyRef> TimelineView::rowKeysInRange(const Row& row, double t0, double t1) const {
    std::vector<KeyRef> out;
    const AnimationClip* clip = document_->clip();
    if (clip == nullptr) {
        return out;
    }
    const auto it = clip->tracks.find(row.bone);
    if (it == clip->tracks.end()) {
        return out;
    }
    for (int c = 0; c < 3; ++c) {
        if (row.channel >= 0 && row.channel != c) {
            continue;
        }
        forKeysInRange(it->second, c, t0, t1, [&](double t) { out.push_back({row.bone, c, t}); });
    }
    return out;
}

std::vector<KeyRef> TimelineView::keysAt(const QPoint& position) const {
    std::vector<KeyRef> out;
    const int index = rowAt(position.y());
    if (index < 0 || position.x() < kLabelWidth) {
        return out;
    }
    const double tolerance = 6.0 / pixelsPerFrame_ / fps();
    const double t = timeForX(position.x());
    auto candidates = rowKeysInRange(rows_[static_cast<std::size_t>(index)], t - tolerance, t + tolerance);
    if (candidates.empty()) {
        return out;
    }
    // La clé la plus proche ; en ligne de résumé, tous les canaux qui ont une clé à ce même temps.
    const KeyRef nearest = *std::min_element(
        candidates.begin(), candidates.end(),
        [&](const KeyRef& a, const KeyRef& b) { return std::abs(a.time - t) < std::abs(b.time - t); });
    for (const KeyRef& k : candidates) {
        if (std::abs(k.time - nearest.time) <= kKeyTimeEpsilon) {
            out.push_back(k);
        }
    }
    return out;
}

std::vector<KeyRef> TimelineView::keysInRect(const QRect& rect) const {
    std::vector<KeyRef> out;
    const double t0 = timeForX(rect.left()), t1 = timeForX(rect.right());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const QRect r = rowRect(static_cast<int>(i));
        if (r.bottom() < rect.top() || r.top() > rect.bottom() || r.bottom() < kRulerHeight) {
            continue;
        }
        const auto keys = rowKeysInRange(rows_[i], t0, t1);
        for (const KeyRef& k : keys) {
            if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
        }
    }
    return out;
}

void TimelineView::selectWithModifiers(const std::vector<KeyRef>& hit, Qt::KeyboardModifiers mods) {
    std::vector<KeyRef> selection =
        (mods & Qt::ControlModifier) ? document_->keySelection() : std::vector<KeyRef>{};
    for (const KeyRef& k : hit) {
        const auto it = std::find(selection.begin(), selection.end(), k);
        if (it == selection.end()) {
            selection.push_back(k);
        } else if (mods & Qt::ControlModifier) {
            selection.erase(it);
        }
    }
    document_->setKeySelection(std::move(selection));
}

// --- Peinture ----------------------------------------------------------------------------------

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const ThemeColors& c = Theme::colors();
    p.fillRect(rect(), c.base);
    paintRows(p);
    paintRuler(p);
    // Tête de lecture.
    const int x = xForTime(document_->time());
    if (x >= kLabelWidth && x <= width() - kScrollBarSize) {
        p.setPen(QPen(QColor(230, 70, 70), 1.5));
        p.drawLine(x, 0, x, height() - kScrollBarSize);
    }
    if (drag_ == Drag::Rectangle) {
        p.setPen(QPen(c.accent, 1, Qt::DashLine));
        p.setBrush(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 40));
        p.drawRect(rubber_.normalized());
    }
}

void TimelineView::paintRuler(QPainter& p) {
    const ThemeColors& c = Theme::colors();
    p.setClipRect(QRect(0, 0, width(), kRulerHeight));
    p.fillRect(QRect(0, 0, width(), kRulerHeight), c.panel);
    p.setPen(c.border);
    p.drawLine(0, kRulerHeight - 1, width(), kRulerHeight - 1);
    const AnimationClip* clip = document_->clip();
    const double f = fps();
    // Plage de lecture.
    if (clip != nullptr && document_->hasPlaybackRange()) {
        const int a = xForTime(document_->rangeStart()), b = xForTime(document_->rangeEnd());
        p.fillRect(QRect(a, 0, b - a, kRulerHeight - 1),
                   QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 60));
    }
    // Graduations : un pas « rond » qui garde au moins 46 px entre deux étiquettes.
    int step = 1;
    for (const int s : {1, 2, 5, 10, 15, 30, 60, 120, 300, 600}) {
        step = s;
        if (s * pixelsPerFrame_ >= 46.0) break;
    }
    const int firstTick = static_cast<int>(std::floor(firstFrame_ / step)) * step;
    const int lastFrame = static_cast<int>(firstFrame_ + (width() - kLabelWidth) / pixelsPerFrame_) + step;
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1.0);
    p.setFont(small);
    for (int frame = firstTick; frame <= lastFrame; frame += step) {
        const int x = kLabelWidth + static_cast<int>(std::lround((frame - firstFrame_) * pixelsPerFrame_));
        if (x < kLabelWidth) continue;
        p.setPen(c.textDim);
        p.drawLine(x, kRulerHeight - 8, x, kRulerHeight - 1);
        p.drawText(QRect(x + 3, 2, 60, kRulerHeight - 10), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(frame));
    }
    // Fin du clip et repères.
    if (clip != nullptr) {
        const int end = xForTime(clip->duration);
        p.setPen(QPen(c.textDim, 1, Qt::DashLine));
        p.drawLine(end, 0, end, height() - kScrollBarSize);
        for (const Marker& m : clip->markers) {
            const int x = xForTime(m.time);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(240, 190, 60));
            p.drawPolygon(QPolygonF({QPointF(x, kRulerHeight - 2), QPointF(x - 4, kRulerHeight - 10),
                                     QPointF(x + 4, kRulerHeight - 10)}));
            p.setPen(QColor(240, 190, 60));
            p.drawText(x + 6, kRulerHeight - 12, QString::fromStdString(m.name));
        }
    }
    // Coin supérieur gauche : image courante.
    p.setClipRect(QRect(0, 0, kLabelWidth, kRulerHeight));
    p.fillRect(QRect(0, 0, kLabelWidth, kRulerHeight), c.panel);
    p.setPen(c.text);
    p.drawText(
        QRect(8, 0, kLabelWidth - 12, kRulerHeight), Qt::AlignVCenter | Qt::AlignLeft,
        QStringLiteral("%1 / %2").arg(document_->frame()).arg(std::max(document_->frameCount() - 1, 0)));
    p.setClipping(false);
    (void)f;
}

void TimelineView::paintRows(QPainter& p) {
    const ThemeColors& c = Theme::colors();
    const AnimationClip* clip = document_->clip();
    const Skeleton* skeleton = document_->skeleton();
    const double t0 = timeForX(kLabelWidth - 10);
    const double t1 = timeForX(width() + 10);
    const auto keyStates = document_->boneKeyStates();
    p.setClipRect(QRect(0, kRulerHeight, width() - kScrollBarSize, height() - kRulerHeight - kScrollBarSize));
    for (int i = scrollRow_; i < static_cast<int>(rows_.size()); ++i) {
        const QRect r = rowRect(i);
        if (r.top() > height()) break;
        const Row& row = rows_[static_cast<std::size_t>(i)];
        const bool boneSelected = skeleton != nullptr && document_->isSelected(row.bone);
        p.fillRect(r, (i % 2) ? c.panel : c.base);
        if (boneSelected) {
            p.fillRect(r, QColor(c.selection.red(), c.selection.green(), c.selection.blue(), 90));
        }
        // Étiquette.
        const int indent = 8 + row.depth * 12;
        p.setPen(c.text);
        QString label;
        if (row.channel < 0) {
            label = QString::fromStdString(skeleton->bone(row.bone).name);
            // Triangle (canaux dépliés ou non).
            p.setPen(c.textDim);
            p.drawText(QRect(indent - 8, r.top(), 12, r.height()), Qt::AlignCenter,
                       isBoneExpanded(row.bone) ? QStringLiteral("▾") : QStringLiteral("▸"));
            p.setPen(row.bone < keyStates.size() && keyStates[row.bone] == BoneKeyState::Keyed
                         ? QColor(255, 225, 60)
                         : (row.bone < keyStates.size() && keyStates[row.bone] == BoneKeyState::Modified
                                ? QColor(255, 140, 30)
                                : c.text));
        } else {
            label = row.channel == ChannelTranslation
                        ? tr("Position")
                        : (row.channel == ChannelRotation ? tr("Rotation") : tr("Scale"));
            p.setPen(c.textDim);
        }
        p.drawText(QRect(indent + 6, r.top(), kLabelWidth - indent - 10, r.height()),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(label, Qt::ElideRight, kLabelWidth - indent - 12));
        p.setPen(c.border);
        p.drawLine(0, r.bottom(), width(), r.bottom());

        // Clés (seulement celles qui sont visibles).
        if (clip == nullptr) continue;
        const auto it = clip->tracks.find(row.bone);
        if (it == clip->tracks.end()) continue;
        std::vector<std::pair<double, bool>> times;  // temps + sélectionnée
        for (int ch = 0; ch < 3; ++ch) {
            if (row.channel >= 0 && row.channel != ch) continue;
            forKeysInRange(it->second, ch, t0, t1,
                           [&](double t) { times.emplace_back(t, isSelected({row.bone, ch, t})); });
        }
        std::sort(times.begin(), times.end());
        const int cy = r.center().y();
        double lastDrawn = -1e9;
        bool lastSelected = false;
        for (std::size_t k = 0; k < times.size(); ++k) {
            const auto& [t, selected] = times[k];
            const bool sameAsPrev = k > 0 && std::abs(t - lastDrawn) <= kKeyTimeEpsilon;
            if (sameAsPrev) {
                lastSelected = lastSelected || selected;
                if (!selected) continue;
            }
            lastDrawn = t;
            const int x = xForTime(t);
            const bool sel = selected || (sameAsPrev && lastSelected);
            const double s = row.channel < 0 ? 5.0 : 4.0;
            QPolygonF diamond(
                {QPointF(x, cy - s), QPointF(x + s, cy), QPointF(x, cy + s), QPointF(x - s, cy)});
            p.setPen(QPen(sel ? QColor(255, 255, 255) : QColor(20, 20, 20), 1));
            p.setBrush(sel ? QColor(255, 190, 40)
                           : (row.channel < 0 ? QColor(235, 235, 235) : QColor(160, 190, 230)));
            p.drawPolygon(diamond);
        }
    }
    p.setClipping(false);
    // Séparateur étiquettes / canevas.
    p.setPen(c.border);
    p.drawLine(kLabelWidth, kRulerHeight, kLabelWidth, height());
}

// --- Souris ------------------------------------------------------------------------------------

void TimelineView::setFrameFromX(int x) {
    const AnimationClip* clip = document_->clip();
    int frame = std::max(frameForX(x), 0);
    if (clip != nullptr) {
        frame = std::min(frame, static_cast<int>(std::ceil(clip->duration * fps())));
    }
    document_->setPlaying(false);
    document_->setFrame(frame);
}

void TimelineView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    pressPos_ = lastPos_ = event->pos();
    const QPoint pos = event->pos();
    if (event->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (pos.y() < kRulerHeight && pos.x() >= kLabelWidth) {
        drag_ = Drag::Scrub;
        setFrameFromX(pos.x());
        return;
    }
    if (pos.x() < kLabelWidth) {
        const int index = rowAt(pos.y());
        if (index < 0) {
            return;
        }
        const Row& row = rows_[static_cast<std::size_t>(index)];
        const int indent = 8 + row.depth * 12;
        if (row.channel < 0 && pos.x() >= indent - 8 && pos.x() <= indent + 6) {
            setBoneExpanded(row.bone, !isBoneExpanded(row.bone));  // triangle : canaux T R S
        } else if (event->modifiers() & Qt::ControlModifier) {
            document_->toggleInSelection(row.bone);
        } else {
            document_->setSelection({row.bone});
        }
        return;
    }
    if (pos.y() < kRulerHeight || document_->clip() == nullptr) {
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {  // décalage global de toutes les clés
        if (document_->beginKeyDrag(allKeys(*document_->clip()), KeyDragMode::Move)) {
            drag_ = Drag::Offset;
        }
        return;
    }
    const std::vector<KeyRef> hit = keysAt(pos);
    if (!hit.empty()) {
        const bool alreadySelected =
            std::all_of(hit.begin(), hit.end(), [&](const KeyRef& k) { return isSelected(k); });
        dragScale_ =
            (event->modifiers() & Qt::ControlModifier) && alreadySelected;  // Ctrl + clé sélectionnée
        dragDuplicate_ = (event->modifiers() & Qt::AltModifier) != 0;
        if (!dragScale_ && !(alreadySelected && !(event->modifiers() & Qt::ControlModifier))) {
            selectWithModifiers(hit, event->modifiers());
        }
        const auto& sel = document_->keySelection();
        pivotTime_ = sel.empty()
                         ? 0.0
                         : std::min_element(sel.begin(), sel.end(), [](const KeyRef& a, const KeyRef& b) {
                               return a.time < b.time;
                           })->time;
        pivotX_ = xForTime(pivotTime_);
        drag_ = Drag::KeyPending;
    } else {
        drag_ = Drag::Rectangle;
        rubber_ = QRect(pos, pos);
        if (!(event->modifiers() & Qt::ControlModifier)) {
            document_->setKeySelection({});
        }
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->pos();
    switch (drag_) {
        case Drag::Scrub:
            setFrameFromX(pos.x());
            break;
        case Drag::Pan:
            setView(firstFrame_ - (pos.x() - lastPos_.x()) / pixelsPerFrame_, pixelsPerFrame_);
            break;
        case Drag::KeyPending:
            if ((pos - pressPos_).manhattanLength() > 3) {
                const KeyDragMode mode = dragScale_
                                             ? KeyDragMode::Scale
                                             : (dragDuplicate_ ? KeyDragMode::Duplicate : KeyDragMode::Move);
                drag_ = document_->beginKeyDrag(document_->keySelection(), mode, pivotTime_) ? Drag::Keys
                                                                                             : Drag::None;
            }
            [[fallthrough]];
        case Drag::Keys:
        case Drag::Offset: {
            if (drag_ == Drag::KeyPending || drag_ == Drag::None) {
                break;
            }
            if (dragScale_ && drag_ == Drag::Keys) {
                const double denominator = pressPos_.x() - pivotX_;
                const double factor = std::abs(denominator) < 4.0 ? 1.0 : (pos.x() - pivotX_) / denominator;
                document_->updateKeyDrag(std::max(factor, 0.05));
            } else {
                const double frames =
                    std::round((pos.x() - pressPos_.x()) / pixelsPerFrame_);  // calé sur les images
                document_->updateKeyDrag(frames / fps());
            }
            break;
        }
        case Drag::Rectangle:
            rubber_.setBottomRight(pos);
            update();
            break;
        case Drag::None:
            break;
    }
    lastPos_ = pos;
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event) {
    switch (drag_) {
        case Drag::Keys:
        case Drag::Offset:
            document_->endKeyDrag();
            break;
        case Drag::Rectangle: {
            std::vector<KeyRef> picked = keysInRect(rubber_.normalized());
            if (event->modifiers() & Qt::ControlModifier) {
                for (const KeyRef& k : document_->keySelection()) {
                    if (std::find(picked.begin(), picked.end(), k) == picked.end()) picked.push_back(k);
                }
            }
            document_->setKeySelection(std::move(picked));
            break;
        }
        default:
            break;
    }
    drag_ = Drag::None;
    dragScale_ = dragDuplicate_ = false;
    update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->pos().x() < kLabelWidth) {
        const int index = rowAt(event->pos().y());
        if (index >= 0 && rows_[static_cast<std::size_t>(index)].channel < 0) {
            const BoneId bone = rows_[static_cast<std::size_t>(index)].bone;
            setBranchCollapsed(bone, !isBranchCollapsed(bone));  // replie / déplie les descendants
        }
    }
}

void TimelineView::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
    if (event->modifiers() & Qt::ControlModifier) {  // défilement vertical des pistes
        vbar_->setValue(vbar_->value() - (delta > 0 ? 1 : -1) * 2);
    } else if (event->modifiers() & Qt::ShiftModifier) {  // panoramique
        setView(firstFrame_ - delta / 120.0 * 8.0 / (pixelsPerFrame_ / 12.0), pixelsPerFrame_);
    } else {
        // Zoom autour du curseur : l'image sous la souris ne bouge pas.
        const double anchor = (event->position().x() - kLabelWidth) / pixelsPerFrame_ + firstFrame_;
        const double zoomed = std::clamp(pixelsPerFrame_ * std::pow(1.15, delta / 120.0), 1.0, 200.0);
        setView(anchor - (event->position().x() - kLabelWidth) / zoomed, zoomed);
    }
    event->accept();
}

bool TimelineView::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        // Suppr / Ctrl+C / Ctrl+V / Ctrl+A concernent les clés quand la timeline a le focus (pas les actions
        // globales).
        const auto* key = static_cast<QKeyEvent*>(e);
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace ||
            (key->modifiers() == Qt::ControlModifier &&
             (key->key() == Qt::Key_C || key->key() == Qt::Key_V || key->key() == Qt::Key_A))) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void TimelineView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        document_->deleteKeys(document_->keySelection());
    } else if (event->matches(QKeySequence::Copy)) {
        document_->copyKeys(document_->keySelection());
    } else if (event->matches(QKeySequence::Paste)) {
        document_->pasteKeys(std::round(document_->time() * fps()) / fps());
    } else if (event->matches(QKeySequence::SelectAll) && document_->clip() != nullptr) {
        document_->setKeySelection(allKeys(*document_->clip()));
    } else {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void TimelineView::contextMenuEvent(QContextMenuEvent* event) {
    if (document_->clip() == nullptr || event->pos().x() < kLabelWidth) {
        return;
    }
    const double time = std::max(std::round(timeForX(event->pos().x()) * fps()) / fps(), 0.0);
    const std::vector<KeyRef> under = keysAt(event->pos());
    if (!under.empty() && !isSelected(under.front())) {
        document_->setKeySelection(under);
    }
    QMenu menu(this);
    const auto selection = document_->keySelection();
    const int index = rowAt(event->pos().y());
    if (index >= 0) {
        const Row& row = rows_[static_cast<std::size_t>(index)];
        menu.addAction(tr("Insert Key Here"), this, [this, row, time] {
            document_->insertKeysAt({row.bone}, row.channel < 0 ? kAllChannels : (1u << row.channel), time);
        });
    }
    QMenu* interpolation = menu.addMenu(tr("Interpolation"));
    interpolation->setEnabled(!selection.empty());
    interpolation->addAction(tr("Constant"), this, [this, selection] {
        document_->setKeysInterpolation(selection, Interpolation::Constant);
    });
    interpolation->addAction(tr("Linear"), this, [this, selection] {
        document_->setKeysInterpolation(selection, Interpolation::Linear);
    });
    interpolation->addAction(tr("Bezier"), this, [this, selection] {
        document_->setKeysInterpolation(selection, Interpolation::Bezier);
    });
    menu.addAction(tr("Delete Keys"), this, [this, selection] { document_->deleteKeys(selection); })
        ->setEnabled(!selection.empty());
    menu.addAction(tr("Copy Keys"), this, [this, selection] { document_->copyKeys(selection); })
        ->setEnabled(!selection.empty());
    menu.addAction(tr("Paste Keys Here"), this, [this, time] { document_->pasteKeys(time); })
        ->setEnabled(document_->hasKeyClipboard());
    menu.addSeparator();
    menu.addAction(tr("Set Range Start Here"), this, [this, time] {
        document_->setPlaybackRange(time, document_->hasPlaybackRange() ? document_->rangeEnd() : -1.0);
    });
    menu.addAction(tr("Set Range End Here"), this,
                   [this, time] { document_->setPlaybackRange(document_->rangeStart(), time); });
    menu.addAction(tr("Clear Range"), this, [this] { document_->setPlaybackRange(0.0, -1.0); });
    menu.addAction(tr("Add Marker Here"), this, [this, time] { document_->addMarker(time, tr("Marker")); });
    menu.addAction(tr("Set Clip End Here"), this, [this, time] { document_->setClipDuration(time); });
    menu.exec(event->globalPos());
}

}  // namespace rigger::editor
