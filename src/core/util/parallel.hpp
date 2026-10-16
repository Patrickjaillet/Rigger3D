// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace rigger {

// Boucle parallèle sans dépendance (std::thread) : découpe [0, count) en blocs contigus traités par
// `body(begin, end)`. En dessous de `minPerThread` éléments par fil, tout est exécuté sur le fil appelant (le
// coût de lancement des fils ne doit jamais dépasser le gain). `body` doit être sûr à appeler depuis
// plusieurs fils sur des blocs disjoints.
template <class Body>
void parallelFor(std::size_t count, std::size_t minPerThread, Body&& body) {
    const std::size_t hardware = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t threads =
        std::min(hardware, std::max<std::size_t>(1, count / std::max<std::size_t>(minPerThread, 1)));
    if (threads <= 1) {
        body(std::size_t{0}, count);
        return;
    }
    const std::size_t chunk = (count + threads - 1) / threads;
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (std::size_t t = 1; t < threads; ++t) {
        const std::size_t begin = t * chunk;
        const std::size_t end = std::min(count, begin + chunk);
        if (begin < end) workers.emplace_back([&body, begin, end] { body(begin, end); });
    }
    body(std::size_t{0}, std::min(count, chunk));
    for (std::thread& w : workers) w.join();
}

}  // namespace rigger
