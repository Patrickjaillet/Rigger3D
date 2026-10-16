// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once

namespace rigger {

// Lecteur d'animation sans Qt : temps courant, lecture / pause / arrêt, boucle, aller-retour (ping-pong),
// vitesse (−2× … +4×) et plage de lecture. L'appelant fournit les secondes écoulées à advance() ; le lecteur
// ne connaît pas le clip lui-même (seulement sa durée), il se contente de calculer le prochain temps.
class AnimationPlayer {
public:
    static constexpr double kMinSpeed = -2.0;
    static constexpr double kMaxSpeed = 4.0;

    // Durée du clip (la plage de travail est ramenée dans [0, durée]).
    void setDuration(double seconds);
    double duration() const { return duration_; }

    // Plage de lecture [start, end] en secondes ; end < 0 : jusqu'à la fin du clip.
    void setRange(double start, double end);
    double rangeStart() const;
    double rangeEnd() const;
    bool hasCustomRange() const { return rangeStart_ > 0.0 || rangeEnd_ >= 0.0; }

    void setSpeed(double speed);  // borné à [-2, 4]
    double speed() const { return speed_; }
    void setLoop(bool loop) { loop_ = loop; }
    bool loop() const { return loop_; }
    void setPingPong(bool pingPong) { pingPong_ = pingPong; }
    bool pingPong() const { return pingPong_; }

    void setTime(double seconds) { time_ = seconds; }
    double time() const { return time_; }
    bool isPlaying() const { return playing_; }
    void play();  // repart du début de la plage si on est à la fin dans le sens de lecture
    void pause() { playing_ = false; }
    void stop();  // arrêt et retour au début de la plage (ou à la fin pour une vitesse négative)

    // Fait avancer le temps de `dt` secondes réelles. Renvoie vrai si la lecture a bouclé / rebondi pendant
    // ce pas. Hors boucle, la lecture s'arrête (playing() devient faux) en atteignant la borne.
    bool advance(double dt);

private:
    double duration_ = 0.0;
    double rangeStart_ = 0.0;
    double rangeEnd_ = -1.0;
    double speed_ = 1.0;
    double time_ = 0.0;
    bool loop_ = true;
    bool pingPong_ = false;
    bool playing_ = false;
    int direction_ = 1;  // +1 / -1 : sens courant en mode ping-pong (multiplie la vitesse)
};

}  // namespace rigger
