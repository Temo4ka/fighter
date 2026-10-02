//===- core/text_file.hpp - Reading data files ------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares readTextFile(), which loads a whole data file (rig,
/// clip, balance JSON) into a string.
///
/// Loaders report problems with exceptions so that a broken file found during
/// a live reload (F5) can be shown to the user without restarting.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

namespace fighter {

/// Returns the contents of \p Path. Throws std::runtime_error naming the file
/// if it cannot be read.
std::string readTextFile(const std::filesystem::path& Path);

} // namespace fighter
