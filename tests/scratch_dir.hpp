//===- scratch_dir.hpp - A temporary directory of one test ------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines ScratchDir, a temporary directory that belongs to one
/// test and is removed with everything in it at the end.
///
/// ctest runs the tests in parallel (-j8), each in its own process, and the
/// test runs of other builds and worktrees share the temporary directory: a
/// fixed name let one test delete or overwrite another one's files mid-test.
/// The name of a ScratchDir is unique (a random part and a counter).
///
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <format>
#include <random>
#include <string_view>
#include <system_error>

namespace fighter::testutil {

/// A path under the temporary directory that no other test uses:
/// fighter_test_<Name>_<random>_<counter>. Nothing is created.
inline std::filesystem::path makeScratchPath(std::string_view Name) {
    static std::atomic<unsigned> Counter{0};
    std::random_device Random;
    return std::filesystem::temp_directory_path() /
           std::format("fighter_test_{}_{:08x}{:08x}_{}", Name, Random(), Random(), Counter++);
}

/// A new empty directory at makeScratchPath(), removed by the destructor.
class ScratchDir {
public:
    explicit ScratchDir(std::string_view Name) : Path(makeScratchPath(Name)) {
        std::filesystem::create_directories(Path);
    }
    ~ScratchDir() {
        std::error_code Ignored;
        std::filesystem::remove_all(Path, Ignored);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    const std::filesystem::path& getPath() const { return Path; }

private:
    std::filesystem::path Path;
};

} // namespace fighter::testutil
