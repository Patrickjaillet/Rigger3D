// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/util/profiler.hpp"

#include <algorithm>
#include <map>
#include <mutex>

namespace rigger::prof {

namespace {

std::atomic<bool> gEnabled{false};
std::mutex gMutex;

std::map<std::string, Stat>& table() {
    static std::map<std::string, Stat> stats;
    return stats;
}

}  // namespace

void setEnabled(bool value) { gEnabled.store(value, std::memory_order_relaxed); }
bool enabled() { return gEnabled.load(std::memory_order_relaxed); }

void record(const char* name, double milliseconds) {
    std::lock_guard<std::mutex> lock(gMutex);
    Stat& s = table()[name];
    if (s.name.empty()) s.name = name;
    ++s.count;
    s.totalMs += milliseconds;
    s.maxMs = std::max(s.maxMs, milliseconds);
    s.lastMs = milliseconds;
}

std::vector<Stat> snapshot() {
    std::vector<Stat> out;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        for (const auto& [name, stat] : table()) out.push_back(stat);
    }
    std::sort(out.begin(), out.end(), [](const Stat& a, const Stat& b) { return a.totalMs > b.totalMs; });
    return out;
}

void reset() {
    std::lock_guard<std::mutex> lock(gMutex);
    table().clear();
}

}  // namespace rigger::prof
