// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QPoint>
#include <QPointF>
#include <QWidget>
#include <optional>
#include <vector>

class QPainter;

#include "core/animation/keys.hpp"
#include "core/skeleton/skeleton.hpp"

namespace rigger::editor {

class Document;

// Éditeur de courbes : composantes X / Y / Z (rouge / vert / bleu) des canaux Position, Rotation (en degrés,
// Euler XYZ — affichage uniquement) et Échelle des os sélectionnés, en fonction du temps. Clés
// sélectionnables (partagées avec la timeline), déplaçables en temps et en valeur, poignées Bézier pour
// Position et Échelle, zoom à la molette, panoramique au clic milieu, « Tout voir ».
class GraphView : public QWidget {
    Q_OBJECT
public:
    // Une courbe = (os, canal, composante 0..2).
    struct Curve {
        BoneId bone = kNoBone;
        int channel = 0;
        int component = 0;
    };
    // Un point de la courbe, tel que dessiné.
    struct Point {
        Curve curve;
        double time = 0.0;
        double value = 0.0;  // radians pour la rotation (affichée en degrés)
    };

    explicit GraphView(Document* document, QWidget* parent = nullptr);

    void setChannelVisible(int channel, bool visible);
    bool isChannelVisible(int channel) const { return (channelMask_ >> channel) & 1u; }
    void fitView();
    // Conversion écran <-> (secondes, valeur affichée : unités ou degrés).
    QPointF toScreen(double seconds, double displayValue) const;
    void setView(double firstSecond, double pixelsPerSecond, double centerValue, double pixelsPerUnit);
    double pixelsPerSecond() const { return pixelsPerSecond_; }
    std::vector<Curve> curves() const;  // courbes visibles (os sélectionnés, canaux actifs)
    // Clé sous le point (tolérance en pixels), si elle existe.
    std::optional<Point> pointAt(const QPoint& position) const;

    QSize sizeHint() const override { return {700, 240}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Drag { None, Pan, Key, HandleIn, HandleOut };

    double displayValue(const Curve& curve, double raw) const;
    double rawValue(const Curve& curve, double display) const;
    glm::vec3 keyValue(const Curve& curve, double time) const;  // valeur brute (Euler pour la rotation)
    std::optional<Point> handleAt(const QPoint& position, bool* outgoing) const;
    QPointF handleScreen(const Point& point, bool outgoing) const;
    void drawCurve(QPainter& p, const Curve& curve) const;

    Document* document_;
    unsigned channelMask_ = 0b111;
    double firstSecond_ = 0.0;
    double pixelsPerSecond_ = 120.0;
    double centerValue_ = 0.0;
    double pixelsPerUnit_ = 80.0;
    Drag drag_ = Drag::None;
    QPoint lastPos_;
    Point active_;
};

}  // namespace rigger::editor
