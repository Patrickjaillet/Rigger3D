// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

// Document : mélange d'animations (Phase 13) : couches, séquenceur NLA, blend tree, rétro-ciblage.
#include <algorithm>

#include "editor/Document.hpp"

namespace rigger::editor {

namespace {
const AnimationMix kNoMix;
}

const AnimationMix& Document::mix() const { return character_ ? character_->mix : kNoMix; }

bool Document::mixActive() const {
    return character_ && mode_ == Mode::Animate && character_->mix.mode != MixMode::Clip &&
           !character_->clips.empty();
}

double Document::timelineDuration() const {
    const AnimationClip* c = clip();
    if (c == nullptr) return 0.0;
    if (!mixActive()) return c->duration;
    const double d = mixDuration(character_->clips, character_->mix);
    return d > 0.0 ? d : c->duration;
}

bool Document::editMix(const std::function<void(AnimationMix&)>& modify, const char* name,
                       const std::string& mergeKey) {
    if (!character_) return false;
    AnimationMix after = character_->mix;
    modify(after);
    if (after == character_->mix) return false;
    const std::string detail;
    runCommand(std::make_unique<EditMixCommand>(character_->mix, std::move(after), name, detail, mergeKey));
    if (clip() == nullptr && !character_->clips.empty())
        setClipIndex(0);  // la timeline suit un clip existant
    syncPlayer();
    recomputePose();
    emit mixChanged();
    emit timeChanged();
    emit poseChanged();
    return true;
}

bool Document::addCrossfade(int clipA, int clipB, double duration) {
    if (!character_) return false;
    const int n = static_cast<int>(character_->clips.size());
    if (clipA < 0 || clipB < 0 || clipA >= n || clipB >= n || duration < 0.0) return false;
    const double lengthA = character_->clips[static_cast<std::size_t>(clipA)].duration;
    const double fade = std::min(duration, lengthA);
    return editMix(
        [&](AnimationMix& m) {
            m.mode = MixMode::Nla;
            NlaStrip a;
            a.name = character_->clips[static_cast<std::size_t>(clipA)].name;
            a.clip = clipA;
            a.start = 0.0;
            NlaStrip b;
            b.name = character_->clips[static_cast<std::size_t>(clipB)].name;
            b.clip = clipB;
            b.start = lengthA - fade;
            b.blendIn = fade;  // B apparaît progressivement par-dessus A
            m.strips = {a, b};
        },
        "Crossfade");
}

Result<int> Document::retargetClipFrom(const Character& source, std::size_t clipIndex, const BoneMap& map,
                                       const RetargetOptions& options) {
    if (!character_) return Error{"no character"};
    if (clipIndex >= source.clips.size()) return Error{"the source character has no such clip"};
    if (map.pairs.empty()) return Error{"no bone could be matched between the two skeletons"};
    AnimationClip clip =
        retargetClip(source.clips[clipIndex], source.skeleton, character_->skeleton, map, options);
    if (clip.tracks.empty()) return Error{"the source clip animates none of the matched bones"};
    clip.name = uniqueClipName(source.clips[clipIndex].name + " (retargeted)");
    const auto before = character_->clips;
    auto after = before;
    after.push_back(clip);
    runCommand(std::make_unique<EditClipListCommand>(before, after, "Retarget Clip", clip.name));
    clipIndex_ = static_cast<int>(after.size()) - 1;
    time_ = 0.0;
    recomputePose();
    syncPlayer();
    emit clipChanged();
    emit timeChanged();
    emit animationChanged();
    return clipIndex_;
}

}  // namespace rigger::editor
