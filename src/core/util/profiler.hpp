// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace rigger::prof {

// Profileur maison (sans dépendance) : des zones nommées (RIGGER_PROFILE_SCOPE) accumulent nombre d'appels,
// temps total, temps max et dernier temps. Désactivé par défaut : une zone désactivée ne coûte qu'un test
// d'entier atomique ; activé, un verrou court protège le tableau (usage de diagnostic, pas de production).

struct Stat {
    std::string name;
    std::uint64_t count = 0;
    double totalMs = 0.0;
    double maxMs = 0.0;
    double lastMs = 0.0;
    double averageMs() const { return count > 0 ? totalMs / static_cast<double>(count) : 0.0; }
};

void setEnabled(bool enabled);
bool enabled();
void record(const char* name, double milliseconds);
std::vector<Stat> snapshot();  // triées par temps total décroissant
void reset();

class ScopedTimer {
public:
    explicit ScopedTimer(const char* name) : name_(name), active_(enabled()) {
        if (active_) start_ = std::chrono::steady_clock::now();
    }
    ~ScopedTimer() {
        if (active_) {
            const auto end = std::chrono::steady_clock::now();
            record(name_, std::chrono::duration<double, std::milli>(end - start_).count());
        }
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* name_;
    bool active_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace rigger::prof

#define RIGGER_PROFILE_CONCAT_INNER(a, b) a##b
#define RIGGER_PROFILE_CONCAT(a, b) RIGGER_PROFILE_CONCAT_INNER(a, b)
#define RIGGER_PROFILE_SCOPE(name) \
    ::rigger::prof::ScopedTimer RIGGER_PROFILE_CONCAT(rigger_profile_, __LINE__)(name)
