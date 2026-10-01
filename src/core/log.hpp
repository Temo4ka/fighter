#pragma once

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

// Логгер с уровнями. Заменяет макросы из старого DSL.hpp: файл открывается один раз,
// а не на каждое сообщение, и лишние уровни отсекаются до форматирования строки.
//
//     log::info("загружено {} текстур", count);
//     log::setFile("logs/fighter.log");
namespace fighter::log {

enum class Level { Debug, Info, Warn, Error };

// Сообщения ниже этого уровня не форматируются и не выводятся.
// По умолчанию: Debug в debug-сборке, Info в release.
void setMinLevel(Level level);
Level minLevel();

// Дублировать вывод в файл (stderr остаётся). Возвращает false, если файл не открылся.
bool setFile(const std::filesystem::path& path);

void write(Level level, std::string_view message);

inline bool enabled(Level level) { return level >= minLevel(); }

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Debug)) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Info)) write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Warn)) write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Error)) write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace fighter::log
