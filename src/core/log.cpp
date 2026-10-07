#include "core/log.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <string>

namespace fighter::log {
namespace {

struct LoggerState {
    std::mutex Mutex;
    Level MinLevel = FIGHTER_DEBUG ? Level::Debug : Level::Info;
    std::ofstream File;
    std::set<std::string, std::less<>> Seen;
    std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
};

LoggerState& getState() {
    static LoggerState State;
    return State;
}

std::string_view getLevelName(Level Severity) {
    switch (Severity) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?    ";
}

} // namespace

void setMinLevel(Level Severity) {
    std::scoped_lock Lock(getState().Mutex);
    getState().MinLevel = Severity;
}

Level getMinLevel() {
    return getState().MinLevel;
}

bool setFile(const std::filesystem::path& Path) {
    std::scoped_lock Lock(getState().Mutex);
    std::error_code Ec;
    if (Path.has_parent_path()) std::filesystem::create_directories(Path.parent_path(), Ec);
    getState().File = std::ofstream(Path, std::ios::out | std::ios::trunc);
    return getState().File.is_open();
}

void write(Level Severity, std::string_view Message) {
    LoggerState& State = getState();
    std::scoped_lock Lock(State.Mutex);

    const double Seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - State.Start).count();
    const std::string Line = std::format("[{:9.3f}] {} {}\n", Seconds, getLevelName(Severity), Message);

    std::fputs(Line.c_str(), stderr);
    if (State.File.is_open()) {
        State.File << Line;
        if (Severity >= Level::Warn) State.File.flush();
    }
}

bool isFirstTime(std::string_view Message) {
    LoggerState& State = getState();
    std::scoped_lock Lock(State.Mutex);
    return State.Seen.emplace(Message).second;
}

} // namespace fighter::log
