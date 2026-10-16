// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <QLineEdit>

namespace rigger::editor {

// Champ numérique « scrubbable » : glisser horizontalement change la valeur (Maj = fin, Ctrl = grossier),
// double-clic pour saisir au clavier (Entrée valide, Échap annule). Une valeur « mixte » (multi-sélection
// aux valeurs différentes) s'affiche « — » ; la première édition l'applique à tout le monde.
class ScrubField : public QLineEdit {
    Q_OBJECT
public:
    explicit ScrubField(QWidget* parent = nullptr);

    double value() const { return value_; }
    void setValue(double value);  // silencieux : n'émet pas valueEdited
    bool isMixed() const { return mixed_; }
    void setMixed(bool mixed);
    // Vrai pendant un glissé ou une saisie : l'appelant ne doit pas écraser le champ.
    bool isBusy() const { return dragging_ || !isReadOnly(); }

    void setRange(double minimum, double maximum);
    void setStep(double perPixel) { step_ = perPixel; }
    void setDecimals(int decimals);
    void setSuffix(const QString& suffix);

    // Formate la valeur comme le champ l'affiche (testable).
    QString formatted(double value) const;

signals:
    void valueEdited(double value);  // uniquement les modifications de l'utilisateur
    void editFinished();             // fin d'un glissé ou d'une saisie validée

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void refreshText();
    void beginTyping();
    void endTyping(bool commit);
    void apply(double value);

    double value_ = 0.0;
    double minimum_ = -1e9;
    double maximum_ = 1e9;
    double step_ = 0.01;
    int decimals_ = 3;
    QString suffix_;
    bool mixed_ = false;
    bool dragging_ = false;
    bool moved_ = false;
    int pressX_ = 0;
    double pressValue_ = 0.0;
};

}  // namespace rigger::editor
