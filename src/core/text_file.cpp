#include "core/text_file.hpp"

#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace fighter {

std::string readTextFile(const std::filesystem::path& Path) {
    std::ifstream File(Path, std::ios::binary);
    if (!File) throw std::runtime_error(std::format("cannot open {}", Path.string()));
    return std::string(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
}

} // namespace fighter
