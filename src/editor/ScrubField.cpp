// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "editor/ScrubField.hpp"

#include <QDoubleValidator>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace rigger::editor {

ScrubField::ScrubField(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setCursor(Qt::SizeHorCursor);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFocusPolicy(Qt::ClickFocus);
    setToolTip(tr("Drag to change the value, double-click to type (Shift: fine, Ctrl: coarse)"));
    refreshText();
}

QString ScrubField::formatted(double value) const {
    return QLocale().toString(value, 'f', decimals_) + suffix_;
}

void ScrubField::refreshText() { setText(mixed_ ? QStringLiteral("—") : formatted(value_)); }

void ScrubField::setValue(double value) {
    value_ = std::clamp(value, minimum_, maximum_);
    mixed_ = false;
    if (isReadOnly()) {
        refreshText();
    }
}

void ScrubField::setMixed(bool mixed) {
    mixed_ = mixed;
    if (isReadOnly()) {
        refreshText();
    }
}

void ScrubField::setRange(double minimum, double maximum) {
    minimum_ = minimum;
    maximum_ = maximum;
    value_ = std::clamp(value_, minimum_, maximum_);
    refreshText();
}

void ScrubField::setDecimals(int decimals) {
    decimals_ = decimals;
    refreshText();
}

void ScrubField::setSuffix(const QString& suffix) {
    suffix_ = suffix;
    refreshText();
}

void ScrubField::apply(double value) {
    value_ = std::clamp(value, minimum_, maximum_);
    mixed_ = false;
    refreshText();
    emit valueEdited(value_);
}

// --- Glissé ---------------------------------------------------------------------------------

void ScrubField::mousePressEvent(QMouseEvent* event) {
    if (!isReadOnly()) {
        QLineEdit::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        moved_ = false;
        pressX_ = event->position().toPoint().x();
        pressValue_ = value_;
        setFocus(Qt::MouseFocusReason);
        event->accept();
    }
}

void ScrubField::mouseMoveEvent(QMouseEvent* event) {
    if (!isReadOnly()) {
        QLineEdit::mouseMoveEvent(event);
        return;
    }
    if (!dragging_) {
        return;
    }
    const int dx = event->position().toPoint().x() - pressX_;
    if (!moved_ && std::abs(dx) < 3) {
        return;  // seuil anti-tremblement : un simple clic ne modifie rien
    }
    moved_ = true;
    double scale = 1.0;
    if (event->modifiers() & Qt::ShiftModifier) {
        scale = 0.1;
    } else if (event->modifiers() & Qt::ControlModifier) {
        scale = 10.0;
    }
    apply(pressValue_ + dx * step_ * scale);
}

void ScrubField::mouseReleaseEvent(QMouseEvent* event) {
    if (!isReadOnly()) {
        QLineEdit::mouseReleaseEvent(event);
        return;
    }
    if (dragging_) {
        dragging_ = false;
        if (moved_) {
            emit editFinished();
        }
        event->accept();
    }
}

// --- Saisie ---------------------------------------------------------------------------------

void ScrubField::mouseDoubleClickEvent(QMouseEvent* event) {
    if (isReadOnly()) {
        dragging_ = false;
        beginTyping();
        event->accept();
        return;
    }
    QLineEdit::mouseDoubleClickEvent(event);
}

void ScrubField::beginTyping() {
    setReadOnly(false);
    setCursor(Qt::IBeamCursor);
    auto* validator = new QDoubleValidator(minimum_, maximum_, 6, this);
    validator->setNotation(QDoubleValidator::StandardNotation);
    setValidator(validator);
    setText(mixed_ ? QString() : QLocale().toString(value_, 'f', decimals_));
    setFocus(Qt::MouseFocusReason);
    selectAll();
}

void ScrubField::endTyping(bool commit) {
    if (isReadOnly()) {
        return;
    }
    const QString typed = text();
    setReadOnly(true);
    setCursor(Qt::SizeHorCursor);
    const QValidator* oldValidator = validator();
    setValidator(nullptr);
    delete oldValidator;
    bool ok = false;
    const double parsed = QLocale().toDouble(typed, &ok);
    if (commit && ok) {
        apply(parsed);
        emit editFinished();
    } else {
        refreshText();
    }
}

void ScrubField::keyPressEvent(QKeyEvent* event) {
    if (!isReadOnly()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            endTyping(true);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            endTyping(false);
            event->accept();
            return;
        }
    }
    QLineEdit::keyPressEvent(event);
}

void ScrubField::focusOutEvent(QFocusEvent* event) {
    endTyping(true);  // quitter le champ valide la saisie
    QLineEdit::focusOutEvent(event);
}

}  // namespace rigger::editor
