// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/GraphView.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "core/math/quat_utils.hpp"
#include "editor/Document.hpp"
#include "editor/Theme.hpp"

namespace rigger::editor {

namespace {

constexpr int kRuler = 18;
constexpr double kPickRadius = 7.0;

QColor componentColor(int component) {
    static const QColor colors[3] = {QColor(235, 90, 90), QColor(110, 205, 95), QColor(95, 150, 245)};
    return colors[component];
}

const Keyframe<glm::vec3>* vecKey(const BoneTracks& tracks, int channel, double time) {
    const auto& track = channel == ChannelScale ? tracks.scale : tracks.translation;
    const auto index = findKey(track, time);
    return index ? &track.keys[*index] : nullptr;
}

double niceStep(double raw) {
    const double magnitude = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
    const double n = raw / magnitude;
    return (n <= 1.0 ? 1.0 : (n <= 2.0 ? 2.0 : (n <= 5.0 ? 5.0 : 10.0))) * magnitude;
}

}  // namespace

GraphView::GraphView(Document* document, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName("graphView");
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(120);
    for (auto signal : {&Document::keySelectionChanged, &Document::timeChanged, &Document::selectionChanged,
                        &Document::animationChanged, &Document::clipChanged}) {
        connect(document_, signal, this, qOverload<>(&QWidget::update));
    }
}

void GraphView::setChannelVisible(int channel, bool visible) {
    channelMask_ = visible ? (channelMask_ | (1u << channel)) : (channelMask_ & ~(1u << channel));
    update();
}

QPointF GraphView::toScreen(double seconds, double displayValue) const {
    return {(seconds - firstSecond_) * pixelsPerSecond_,
            height() * 0.5 + kRuler * 0.5 - (displayValue - centerValue_) * pixelsPerUnit_};
}

void GraphView::setView(double firstSecond, double pixelsPerSecond, double centerValue,
                        double pixelsPerUnit) {
    firstSecond_ = firstSecond;
    pixelsPerSecond_ = std::clamp(pixelsPerSecond, 5.0, 5000.0);
    centerValue_ = centerValue;
    pixelsPerUnit_ = std::clamp(pixelsPerUnit, 0.5, 100000.0);
    update();
}

double GraphView::displayValue(const Curve& curve, double raw) const {
    return curve.channel == ChannelRotation ? glm::degrees(raw) : raw;
}

double GraphView::rawValue(const Curve& curve, double display) const {
    return curve.channel == ChannelRotation ? glm::radians(display) : display;
}

std::vector<GraphView::Curve> GraphView::curves() const {
    std::vector<Curve> out;
    const AnimationClip* clip = document_->clip();
    if (clip == nullptr) {
        return out;
    }
    for (const BoneId bone : document_->selection()) {
        const auto it = clip->tracks.find(bone);
        if (it == clip->tracks.end()) {
            continue;
        }
        for (int channel = 0; channel < 3; ++channel) {
            const bool keyed = channel == ChannelRotation ? !it->second.rotation.empty()
                               : channel == ChannelScale  ? !it->second.scale.empty()
                                                          : !it->second.translation.empty();
            if (keyed && isChannelVisible(channel)) {
                for (int c = 0; c < 3; ++c) out.push_back({bone, channel, c});
            }
        }
    }
    return out;
}

glm::vec3 GraphView::keyValue(const Curve& curve, double time) const {
    const AnimationClip* clip = document_->clip();
    const auto it = clip->tracks.find(curve.bone);
    if (it == clip->tracks.end()) {
        return {};
    }
    if (curve.channel == ChannelRotation) {
        const auto index = findKey(it->second.rotation, time);
        return index ? toEulerXYZ(it->second.rotation.keys[*index].value) : glm::vec3(0.0f);
    }
    const auto* key = vecKey(it->second, curve.channel, time);
    return key != nullptr ? key->value : glm::vec3(0.0f);
}

void GraphView::fitView() {
    double t0 = 1e30, t1 = -1e30, v0 = 1e30, v1 = -1e30;
    const AnimationClip* clip = document_->clip();
    for (const Curve& curve : curves()) {
        const BoneTracks& tracks = clip->tracks.at(curve.bone);
        const auto visit = [&](double t, double raw) {
            const double v = displayValue(curve, raw);
            t0 = std::min(t0, t), t1 = std::max(t1, t);
            v0 = std::min(v0, v), v1 = std::max(v1, v);
        };
        if (curve.channel == ChannelRotation) {
            for (const auto& k : tracks.rotation.keys) visit(k.time, toEulerXYZ(k.value)[curve.component]);
        } else {
            for (const auto& k : (curve.channel == ChannelScale ? tracks.scale : tracks.translation).keys) {
                visit(k.time, k.value[curve.component]);
            }
        }
    }
    if (t0 > t1) {
        t0 = 0.0, t1 = clip != nullptr ? std::max(clip->duration, 1.0) : 1.0, v0 = -1.0, v1 = 1.0;
    }
    const double dt = std::max(t1 - t0, 0.1), dv = std::max(v1 - v0, 1e-3);
    const double w = std::max(width() - 40, 60), h = std::max(height() - kRuler - 40, 60);
    setView(t0 - 20.0 / (w / dt), w / dt, (v0 + v1) * 0.5, h / dv);
}

std::optional<GraphView::Point> GraphView::pointAt(const QPoint& position) const {
    const AnimationClip* clip = document_->clip();
    std::optional<Point> best;
    double bestDistance = kPickRadius;
    for (const Curve& curve : curves()) {
        const BoneTracks& tracks = clip->tracks.at(curve.bone);
        const auto test = [&](double t, double raw) {
            const QPointF s = toScreen(t, displayValue(curve, raw));
            const double d = std::hypot(s.x() - position.x(), s.y() - position.y());
            if (d <= bestDistance) {
                bestDistance = d;
                best = Point{curve, t, raw};
            }
        };
        if (curve.channel == ChannelRotation) {
            for (const auto& k : tracks.rotation.keys) test(k.time, toEulerXYZ(k.value)[curve.component]);
        } else {
            for (const auto& k : (curve.channel == ChannelScale ? tracks.scale : tracks.translation).keys) {
                test(k.time, k.value[curve.component]);
            }
        }
    }
    return best;
}

QPointF GraphView::handleScreen(const Point& point, bool outgoing) const {
    const BoneTracks& tracks = document_->clip()->tracks.at(point.curve.bone);
    const auto& track = point.curve.channel == ChannelScale ? tracks.scale : tracks.translation;
    const auto index = findKey(track, point.time);
    if (!index) {
        return {};
    }
    const std::size_t i = *index;
    const auto& key = track.keys[i];
    const int c = point.curve.component;
    if (outgoing) {
        const double h = (track.keys[i + 1].time - key.time) / 3.0;
        return toScreen(key.time + h, key.value[c] + key.outTangent[c] * h);
    }
    const double h = (key.time - track.keys[i - 1].time) / 3.0;
    return toScreen(key.time - h, key.value[c] - key.inTangent[c] * h);
}

std::optional<GraphView::Point> GraphView::handleAt(const QPoint& position, bool* outgoing) const {
    const AnimationClip* clip = document_->clip();
    for (const KeyRef& ref : document_->keySelection()) {
        if (ref.channel == ChannelRotation) {
            continue;
        }
        const auto it = clip->tracks.find(ref.bone);
        if (it == clip->tracks.end()) {
            continue;
        }
        const auto& track = ref.channel == ChannelScale ? it->second.scale : it->second.translation;
        const auto index = findKey(track, ref.time);
        if (!index) {
            continue;
        }
        for (int c = 0; c < 3; ++c) {
            const Curve curve{ref.bone, ref.channel, c};
            if (!isChannelVisible(ref.channel)) {
                continue;
            }
            Point point{curve, track.keys[*index].time, track.keys[*index].value[c]};
            for (const bool out : {true, false}) {
                const bool exists =
                    out ? (track.keys[*index].mode == Interpolation::Bezier && *index + 1 < track.keys.size())
                        : (*index > 0 && track.keys[*index - 1].mode == Interpolation::Bezier);
                if (!exists) continue;
                const QPointF s = handleScreen(point, out);
                if (std::hypot(s.x() - position.x(), s.y() - position.y()) <= kPickRadius) {
                    *outgoing = out;
                    return point;
                }
            }
        }
    }
    return std::nullopt;
}

void GraphView::drawCurve(QPainter& p, const Curve& curve) const {
    const AnimationClip* clip = document_->clip();
    const BoneTracks& tracks = clip->tracks.at(curve.bone);
    QPainterPath path;
    bool first = true;
    for (int x = 0; x <= width(); x += 3) {
        const double t = firstSecond_ + x / pixelsPerSecond_;
        double raw = 0.0;
        if (curve.channel == ChannelRotation) {
            raw = toEulerXYZ(sampleTrack(tracks.rotation, t))[curve.component];
        } else {
            raw = sampleTrack(curve.channel == ChannelScale ? tracks.scale : tracks.translation,
                              t)[curve.component];
        }
        const QPointF s = toScreen(t, displayValue(curve, raw));
        if (first) {
            path.moveTo(s);
            first = false;
        } else {
            path.lineTo(s);
        }
    }
    p.setPen(QPen(componentColor(curve.component), 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void GraphView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const ThemeColors& c = Theme::colors();
    p.fillRect(rect(), c.base);
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() - 1.0);
    p.setFont(small);
    // Grille.
    const double stepT = niceStep(70.0 / pixelsPerSecond_);
    for (double t = std::floor(firstSecond_ / stepT) * stepT; toScreen(t, 0).x() < width(); t += stepT) {
        const double x = toScreen(t, 0).x();
        p.setPen(c.border);
        p.drawLine(QPointF(x, kRuler), QPointF(x, height()));
        p.setPen(c.textDim);
        p.drawText(QPointF(x + 3, 12), QStringLiteral("%1 s").arg(t, 0, 'g', 4));
    }
    const double stepV = niceStep(40.0 / pixelsPerUnit_);
    const double halfRange = height() * 0.5 / pixelsPerUnit_ + stepV;
    for (double v = std::floor((centerValue_ - halfRange) / stepV) * stepV; v < centerValue_ + halfRange;
         v += stepV) {
        const double y = toScreen(0, v).y();
        if (y < kRuler || y > height()) continue;
        p.setPen(std::abs(v) < 1e-9 ? c.textDim : c.border);
        p.drawLine(QPointF(0, y), QPointF(width(), y));
        p.setPen(c.textDim);
        p.drawText(QPointF(3, y - 2), QString::number(v, 'g', 5));
    }
    const AnimationClip* clip = document_->clip();
    if (clip == nullptr || document_->selection().empty()) {
        p.setPen(c.textDim);
        p.drawText(rect(), Qt::AlignCenter,
                   clip == nullptr ? tr("No clip: key a pose to create one")
                                   : tr("Select bones to see their curves"));
    } else {
        const auto all = curves();
        for (const Curve& curve : all) {
            drawCurve(p, curve);
        }
        const auto& selection = document_->keySelection();
        for (const Curve& curve : all) {
            const BoneTracks& tracks = clip->tracks.at(curve.bone);
            const auto drawKey = [&](double t, double raw, bool selected) {
                const QPointF s = toScreen(t, displayValue(curve, raw));
                if (s.x() < -8 || s.x() > width() + 8) return;
                p.setPen(QPen(selected ? QColor(255, 255, 255) : QColor(20, 20, 20), 1));
                p.setBrush(selected ? QColor(255, 190, 40) : componentColor(curve.component));
                p.drawEllipse(s, 3.5, 3.5);
            };
            const auto isSel = [&](double t) {
                return std::any_of(selection.begin(), selection.end(), [&](const KeyRef& k) {
                    return k == KeyRef{curve.bone, curve.channel, t};
                });
            };
            if (curve.channel == ChannelRotation) {
                for (const auto& k : tracks.rotation.keys)
                    drawKey(k.time, toEulerXYZ(k.value)[curve.component], isSel(k.time));
            } else {
                for (const auto& k :
                     (curve.channel == ChannelScale ? tracks.scale : tracks.translation).keys) {
                    drawKey(k.time, k.value[curve.component], isSel(k.time));
                }
            }
        }
        // Poignées Bézier des clés sélectionnées.
        for (const KeyRef& ref : selection) {
            if (ref.channel == ChannelRotation || !isChannelVisible(ref.channel) ||
                !document_->isSelected(ref.bone)) {
                continue;
            }
            const auto it = clip->tracks.find(ref.bone);
            if (it == clip->tracks.end()) continue;
            const auto& track = ref.channel == ChannelScale ? it->second.scale : it->second.translation;
            const auto index = findKey(track, ref.time);
            if (!index) continue;
            for (int comp = 0; comp < 3; ++comp) {
                const Point point{
                    {ref.bone, ref.channel, comp}, track.keys[*index].time, track.keys[*index].value[comp]};
                const QPointF origin = toScreen(point.time, point.value);
                for (const bool out : {true, false}) {
                    const bool exists =
                        out ? (track.keys[*index].mode == Interpolation::Bezier &&
                               *index + 1 < track.keys.size())
                            : (*index > 0 && track.keys[*index - 1].mode == Interpolation::Bezier);
                    if (!exists) continue;
                    const QPointF h = handleScreen(point, out);
                    p.setPen(QPen(componentColor(comp), 1, Qt::DotLine));
                    p.drawLine(origin, h);
                    p.setBrush(c.base);
                    p.drawRect(QRectF(h.x() - 3, h.y() - 3, 6, 6));
                }
            }
        }
    }
    // Barre de temps + tête de lecture.
    p.fillRect(QRect(0, 0, width(), kRuler), Qt::transparent);
    const double x = toScreen(document_->time(), 0).x();
    p.setPen(QPen(QColor(230, 70, 70), 1.5));
    p.drawLine(QPointF(x, 0), QPointF(x, height()));
}

void GraphView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    lastPos_ = event->pos();
    if (event->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        return;
    }
    if (event->button() != Qt::LeftButton || document_->clip() == nullptr) {
        return;
    }
    if (event->pos().y() < kRuler) {  // règle : déplacer la tête de lecture
        document_->setPlaying(false);
        document_->setFrame(static_cast<int>(
            std::lround((firstSecond_ + event->pos().x() / pixelsPerSecond_) * document_->fps())));
        return;
    }
    bool outgoing = false;
    if (const auto handle = handleAt(event->pos(), &outgoing)) {
        active_ = *handle;
        drag_ = outgoing ? Drag::HandleOut : Drag::HandleIn;
        return;
    }
    if (const auto point = pointAt(event->pos())) {
        active_ = *point;
        const KeyRef ref{point->curve.bone, point->curve.channel, point->time};
        auto selection =
            (event->modifiers() & Qt::ControlModifier) ? document_->keySelection() : std::vector<KeyRef>{};
        const auto found = std::find(selection.begin(), selection.end(), ref);
        if (found == selection.end()) {
            selection.push_back(ref);
        } else if (event->modifiers() & Qt::ControlModifier) {
            selection.erase(found);
        }
        document_->setKeySelection(std::move(selection));
        drag_ = Drag::Key;
        return;
    }
    document_->setKeySelection({});
}

void GraphView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->pos();
    if (drag_ == Drag::Pan) {
        setView(firstSecond_ - (pos.x() - lastPos_.x()) / pixelsPerSecond_, pixelsPerSecond_,
                centerValue_ + (pos.y() - lastPos_.y()) / pixelsPerUnit_, pixelsPerUnit_);
    } else if (drag_ == Drag::Key) {
        const double fps = document_->fps();
        const double seconds =
            std::max(std::round((firstSecond_ + pos.x() / pixelsPerSecond_) * fps) / fps, 0.0);
        const double display = centerValue_ + (height() * 0.5 + kRuler * 0.5 - pos.y()) / pixelsPerUnit_;
        glm::vec3 value = keyValue(active_.curve, active_.time);
        value[active_.curve.component] = static_cast<float>(rawValue(active_.curve, display));
        KeyEdit edit;
        edit.time = seconds;
        edit.value = value;
        const KeyRef ref{active_.curve.bone, active_.curve.channel, active_.time};
        if (document_->editKey(ref, edit, true)) {
            active_.time = seconds;
        }
    } else if (drag_ == Drag::HandleIn || drag_ == Drag::HandleOut) {
        const bool out = drag_ == Drag::HandleOut;
        const AnimationClip* clip = document_->clip();
        const auto it = clip->tracks.find(active_.curve.bone);
        if (it != clip->tracks.end()) {
            const auto* key = vecKey(it->second, active_.curve.channel, active_.time);
            const auto& track =
                active_.curve.channel == ChannelScale ? it->second.scale : it->second.translation;
            const auto index = findKey(track, active_.time);
            if (key != nullptr && index) {
                const std::size_t i = *index;
                const double h =
                    out ? (i + 1 < track.keys.size() ? (track.keys[i + 1].time - key->time) / 3.0 : 0.0)
                        : (i > 0 ? (key->time - track.keys[i - 1].time) / 3.0 : 0.0);
                const double display =
                    centerValue_ + (height() * 0.5 + kRuler * 0.5 - pos.y()) / pixelsPerUnit_;
                if (h > 1e-6) {
                    const int comp = active_.curve.component;
                    const double slope = (display - key->value[comp]) / h * (out ? 1.0 : -1.0);
                    glm::vec3 tangent = out ? key->outTangent : key->inTangent;
                    tangent[comp] = static_cast<float>(slope);
                    KeyEdit edit;
                    (out ? edit.outTangent : edit.inTangent) = tangent;
                    document_->editKey({active_.curve.bone, active_.curve.channel, active_.time}, edit, true);
                }
            }
        }
    }
    lastPos_ = pos;
}

void GraphView::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ == Drag::Key || drag_ == Drag::HandleIn || drag_ == Drag::HandleOut) {
        document_->endContinuousEdit();
    }
    drag_ = Drag::None;
}

void GraphView::wheelEvent(QWheelEvent* event) {
    const double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
    const double anchorT = firstSecond_ + event->position().x() / pixelsPerSecond_;
    const double anchorV =
        centerValue_ + (height() * 0.5 + kRuler * 0.5 - event->position().y()) / pixelsPerUnit_;
    double newPps = pixelsPerSecond_, newPpu = pixelsPerUnit_;
    if (!(event->modifiers() & Qt::ShiftModifier)) newPps *= factor;    // Maj : valeur seule
    if (!(event->modifiers() & Qt::ControlModifier)) newPpu *= factor;  // Ctrl : temps seul
    newPps = std::clamp(newPps, 5.0, 5000.0);
    newPpu = std::clamp(newPpu, 0.5, 100000.0);
    setView(anchorT - event->position().x() / newPps, newPps,
            anchorV - (height() * 0.5 + kRuler * 0.5 - event->position().y()) / newPpu, newPpu);
    event->accept();
}

void GraphView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F || event->key() == Qt::Key_Home) {
        fitView();
    } else if (event->key() == Qt::Key_Delete) {
        document_->deleteKeys(document_->keySelection());
    } else {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

}  // namespace rigger::editor
