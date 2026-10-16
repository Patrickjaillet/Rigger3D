// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "core/skinning/weights.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rigger {

Result<void> normalizeWeights(SkinWeight& skin) {
    float sum = 0.0f;
    for (std::size_t i = 0; i < skin.weights.size(); ++i) {
        const float weight = skin.weights[i];
        if (!std::isfinite(weight) || weight < 0.0f) {
            return Error{"skin weights must be finite and non-negative"};
        }
        if (weight > 0.0f && skin.joints[i] == kNoBone) {
            return Error{"skin joint index cannot be kNoBone when weight is positive"};
        }
        sum += weight;
    }
    if (!std::isfinite(sum) || sum <= 0.0f) {
        return Error{"cannot normalize skin weights with a non-positive sum"};
    }
    for (std::size_t i = 0; i < skin.weights.size(); ++i) {
        if (skin.weights[i] == 0.0f) {
            skin.joints[i] = kNoBone;
        } else {
            skin.weights[i] /= sum;
        }
    }
    return {};
}

Result<void> limitInfluences(SkinWeight& skin, std::size_t maxInfluences) {
    if (maxInfluences == 0 || maxInfluences > skin.weights.size()) {
        return Error{"maximum influences must be between 1 and 4"};
    }
    for (const float weight : skin.weights) {
        if (!std::isfinite(weight) || weight < 0.0f) {
            return Error{"skin weights must be finite and non-negative"};
        }
    }

    std::array<std::size_t, 4> order{0, 1, 2, 3};
    std::stable_sort(order.begin(), order.end(),
                     [&skin](std::size_t a, std::size_t b) { return skin.weights[a] > skin.weights[b]; });
    SkinWeight limited;
    for (std::size_t i = 0; i < maxInfluences; ++i) {
        limited.joints[i] = skin.joints[order[i]];
        limited.weights[i] = skin.weights[order[i]];
    }
    const Result<void> normalized = normalizeWeights(limited);
    if (!normalized) {
        return normalized.error();
    }
    skin = limited;
    return {};
}

Result<void> pruneWeights(SkinWeight& skin, float threshold) {
    if (!std::isfinite(threshold) || threshold < 0.0f) {
        return Error{"weight pruning threshold must be finite and non-negative"};
    }
    for (const float weight : skin.weights) {
        if (!std::isfinite(weight) || weight < 0.0f) {
            return Error{"skin weights must be finite and non-negative"};
        }
    }

    SkinWeight pruned = skin;
    for (std::size_t i = 0; i < pruned.weights.size(); ++i) {
        if (pruned.weights[i] < threshold) {
            pruned.weights[i] = 0.0f;
            pruned.joints[i] = kNoBone;
        }
    }
    if (std::all_of(pruned.weights.begin(), pruned.weights.end(),
                    [](float weight) { return weight == 0.0f; })) {
        return Error{"pruning removed every skin influence"};
    }
    const Result<void> normalized = normalizeWeights(pruned);
    if (!normalized) {
        return normalized.error();
    }
    skin = pruned;
    return {};
}

}  // namespace rigger
