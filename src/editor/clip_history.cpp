#include "editor/clip_history.hpp"

#include <algorithm>

namespace fighter::editor {
namespace {

bool isSamePose(const anim::Pose& Left, const anim::Pose& Right);

} // namespace

bool isSameClip(const anim::Clip& Left, const anim::Clip& Right) {
    const bool SameKeys =
        std::ranges::equal(Left.Keys, Right.Keys, [](const anim::Keyframe& First, const anim::Keyframe& Second) {
            return First.TimeSec == Second.TimeSec && isSamePose(First.Target, Second.Target);
        });
    const bool SamePelvis =
        std::ranges::equal(Left.PelvisTrack, Right.PelvisTrack, [](const anim::PelvisKey& First, const anim::PelvisKey& Second) {
            return First.TimeSec == Second.TimeSec && First.OffsetX == Second.OffsetX;
        });
    return SameKeys && SamePelvis && Left.Name == Right.Name && Left.DurationSec == Right.DurationSec &&
           Left.Loop == Right.Loop && Left.ActiveBeginSec == Right.ActiveBeginSec &&
           Left.ActiveEndSec == Right.ActiveEndSec && Left.Strikers == Right.Strikers &&
           Left.Stiffness == Right.Stiffness && Left.AllowMove == Right.AllowMove &&
           Left.BlendInSec == Right.BlendInSec && Left.BlendOutSec == Right.BlendOutSec;
}

void ClipHistory::reset(const anim::Clip& Start) {
    resetUnsaved(Start);
    Saved = 0;
}

void ClipHistory::resetUnsaved(const anim::Clip& Start) {
    States.assign(1, Start);
    Current = 0;
    Saved.reset();
}

bool ClipHistory::commit(const anim::Clip& Now) {
    if (States.empty()) {
        resetUnsaved(Now);
        return true;
    }
    if (isSameClip(States[Current], Now)) return false;
    States.resize(Current + 1);
    // A saved state that could have been redone is gone.
    if (Saved && *Saved > Current) Saved.reset();
    States.push_back(Now);
    ++Current;
    if (States.size() > MaxClipHistorySteps) {
        States.erase(States.begin());
        --Current;
        if (Saved) {
            if (*Saved == 0) {
                Saved.reset();
            } else {
                --*Saved;
            }
        }
    }
    return true;
}

const anim::Clip* ClipHistory::undo() {
    if (!canUndo()) return nullptr;
    return &States[--Current];
}

const anim::Clip* ClipHistory::redo() {
    if (!canRedo()) return nullptr;
    return &States[++Current];
}

namespace {

bool isSamePose(const anim::Pose& Left, const anim::Pose& Right) {
    return Left.Angles == Right.Angles && Left.Mask == Right.Mask && Left.WeaponAngle == Right.WeaponAngle &&
           Left.HasWeapon == Right.HasWeapon;
}

} // namespace

} // namespace fighter::editor
