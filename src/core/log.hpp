//===- core/log.hpp - Leveled logger ----------------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the project logger: messages with a severity level,
/// written to stderr and optionally duplicated to a file.
///
/// Messages below the minimum level are discarded before formatting, so
/// debug-level logging in hot code costs only a comparison. The log file is
/// opened once rather than per message.
///
/// \code
///   log::info("loaded {} textures", Count);
///   log::setFile("logs/fighter.log");
/// \endcode
///
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace fighter::log {

enum class Level { Debug, Info, Warn, Error };

/// Sets the lowest level that is still written. The default is Debug in the
/// debug build and Info in the release build.
void setMinLevel(Level Severity);
Level getMinLevel();

/// Duplicates output to \p Path (stderr output continues). Returns false if
/// the file cannot be opened.
bool setFile(const std::filesystem::path& Path);

void write(Level Severity, std::string_view Message);

inline bool isEnabled(Level Severity) { return Severity >= getMinLevel(); }

template <class... Args>
void debug(std::format_string<Args...> Fmt, Args&&... Arguments) {
    if (isEnabled(Level::Debug))
        write(Level::Debug, std::format(Fmt, std::forward<Args>(Arguments)...));
}

template <class... Args>
void info(std::format_string<Args...> Fmt, Args&&... Arguments) {
    if (isEnabled(Level::Info))
        write(Level::Info, std::format(Fmt, std::forward<Args>(Arguments)...));
}

template <class... Args>
void warn(std::format_string<Args...> Fmt, Args&&... Arguments) {
    if (isEnabled(Level::Warn))
        write(Level::Warn, std::format(Fmt, std::forward<Args>(Arguments)...));
}

template <class... Args>
void error(std::format_string<Args...> Fmt, Args&&... Arguments) {
    if (isEnabled(Level::Error))
        write(Level::Error, std::format(Fmt, std::forward<Args>(Arguments)...));
}

} // namespace fighter::log
