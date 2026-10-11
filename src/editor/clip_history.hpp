//===- editor/clip_history.hpp - Undo and redo of a clip --------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares ClipHistory, the undo/redo history of the pose editor:
/// a list of snapshots of the whole clip. The window calls commit() with the
/// clip as it is once an edit is over (a mouse drag or a slider drag is over
/// when the button is released), so one gesture is one step; commit() with a
/// clip equal to the current state does nothing. undo() and redo() hand back a
/// snapshot to replace the edited clip with.
///
/// The history also knows which state is on the disk (markSaved()): after an
/// undo back to it isDirty() is false again. Opening another clip starts a new
/// history (reset()). The history keeps at most MaxClipHistorySteps snapshots;
/// the oldest are dropped (and, if the saved state is dropped, every state is
/// unsaved).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "anim/clip.hpp"

namespace fighter::editor {

/// The most snapshots the history keeps.
inline constexpr size_t MaxClipHistorySteps = 200;

/// Are the two clips the same in every field (exactly)?
bool isSameClip(const anim::Clip& Left, const anim::Clip& Right);

class ClipHistory {
public:
    /// Starts a history at \p Start, which is the state on the disk.
    void reset(const anim::Clip& Start);
    /// Starts a history at \p Start, which is not on the disk (a new clip).
    void resetUnsaved(const anim::Clip& Start);

    /// Records \p Now as the new state unless it equals the current one; the
    /// states that could be redone are dropped. True if a step was added.
    bool commit(const anim::Clip& Now);

    bool canUndo() const { return Current > 0; }
    bool canRedo() const { return Current + 1 < States.size(); }
    /// The previous state, now the current one, or nullptr if none.
    const anim::Clip* undo();
    /// The next state, now the current one, or nullptr if none.
    const anim::Clip* redo();

    /// The current state is the one on the disk.
    void markSaved() { Saved = Current; }
    /// Is the current state different from the one on the disk?
    bool isDirty() const { return !Saved || *Saved != Current; }

    /// The current state; reset() must have been called.
    const anim::Clip& getCurrent() const { return States[Current]; }
    size_t getStepCount() const { return States.size(); }

private:
    std::vector<anim::Clip> States;
    size_t Current = 0;
    std::optional<size_t> Saved;
};

} // namespace fighter::editor
