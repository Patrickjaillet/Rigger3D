// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/animation/player.hpp"

#include <algorithm>
#include <cmath>

namespace rigger {

void AnimationPlayer::setDuration(double seconds) {
    duration_ = std::max(seconds, 0.0);
    time_ = std::clamp(time_, 0.0, std::max(duration_, time_));  // le temps peut dépasser pendant l'édition
}

void AnimationPlayer::setRange(double start, double end) {
    rangeStart_ = std::max(start, 0.0);
    rangeEnd_ = end;
}

double AnimationPlayer::rangeStart() const { return std::clamp(rangeStart_, 0.0, duration_); }

double AnimationPlayer::rangeEnd() const {
    const double end = rangeEnd_ < 0.0 ? duration_ : std::min(rangeEnd_, duration_);
    return std::max(end, rangeStart());
}

void AnimationPlayer::setSpeed(double speed) { speed_ = std::clamp(speed, kMinSpeed, kMaxSpeed); }

void AnimationPlayer::play() {
    const double effective = speed_ * direction_;
    if (effective >= 0.0 && time_ >= rangeEnd()) {
        time_ = rangeStart();
    } else if (effective < 0.0 && time_ <= rangeStart()) {
        time_ = rangeEnd();
    }
    playing_ = true;
}

void AnimationPlayer::stop() {
    playing_ = false;
    direction_ = 1;
    time_ = speed_ < 0.0 ? rangeEnd() : rangeStart();
}

bool AnimationPlayer::advance(double dt) {
    if (!playing_ || dt <= 0.0) {
        return false;
    }
    const double start = rangeStart();
    const double end = rangeEnd();
    const double length = end - start;
    double t = time_ + dt * speed_ * direction_;
    bool wrapped = false;

    if (length <= 0.0) {  // plage vide : rien à parcourir
        time_ = start;
        playing_ = loop_ || pingPong_;
        return false;
    }
    if (t > end || t < start) {
        if (pingPong_) {
            // Dépliage : k demi-tours complets ont été parcourus ; k impair = on repart dans l'autre sens.
            const double unfolded = t - start;
            const double k = std::floor(unfolded / length);
            const double offset = unfolded - k * length;
            const bool odd = (static_cast<long long>(k) % 2) != 0;
            t = start + (odd ? length - offset : offset);
            if (odd) {
                direction_ = -direction_;
            }
            wrapped = true;
        } else if (loop_) {
            double offset = std::fmod(t - start, length);
            if (offset < 0.0) offset += length;
            t = start + offset;
            wrapped = true;
        } else {
            t = t > end ? end : start;
            playing_ = false;
        }
    }
    time_ = t;
    return wrapped;
}

}  // namespace rigger
