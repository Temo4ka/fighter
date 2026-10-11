//===- editor/new_clip.hpp - A new clip from a template ---------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares what the pose editor needs to start a new clip from a
/// template: checking the name (a file name of data/poses that does not exist
/// yet) and making the clip. Nothing is written here: the clip becomes a file
/// when the user saves it in the window (editor::saveClip()).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "anim/clip.hpp"

namespace fighter::editor {

/// The longest name of a new clip.
inline constexpr size_t MaxClipNameLength = 48;

/// What is wrong with \p Name as the name of a new clip in
/// \p DataDir / "poses", or an empty string: it must be 1..MaxClipNameLength
/// characters of a-z, 0-9 and '_' (the style of the existing files), start
/// with a letter, and no clip of that name may exist.
std::string findNewClipProblem(const std::filesystem::path& DataDir, std::string_view Name);

/// A new clip named \p Name that has the keys, the timing and the fields of
/// \p Template (a stance of a weapon or another clip). Its strikers and
/// active phase are copied too: the Warnings panel tells what does not fit.
anim::Clip makeClipFromTemplate(const anim::Clip& Template, std::string Name);

} // namespace fighter::editor
