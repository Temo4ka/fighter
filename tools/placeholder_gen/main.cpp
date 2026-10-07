#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include "core/log.hpp"
#include "placeholder_gen/placeholder_gen.hpp"

// Command line arguments:
//   --data <dir>        directory with rigs/, items/ and visuals.json (default <project>/data)
//   --out <dir>         output directory (default <project>/assets/placeholders)
//   --rig <name>        rig file data/rigs/<name>.json (default humanoid)
//   --variant <name>    pixel, smooth or all (default all)
//   --preview <file>    also write a contact sheet of the assembled fighter (default: none)
//   --overlap <share>   joint overlap as a share of the part length (default 0)
//
// The default directories are the source tree known at build time, so running
// the tool without arguments regenerates the committed pictures.
int main(int Argc, char** Argv) {
    using namespace fighter;

    tools::GenerateOptions Options;
    Options.DataDir = std::filesystem::path(FIGHTER_SOURCE_DIR) / "data";
    Options.OutDir = std::filesystem::path(FIGHTER_SOURCE_DIR) / "assets" / "placeholders";

    for (int ArgIndex = 1; ArgIndex < Argc; ++ArgIndex) {
        const std::string_view Arg = Argv[ArgIndex];
        const bool HasValue = ArgIndex + 1 < Argc;
        if (Arg == "--data" && HasValue) {
            Options.DataDir = Argv[++ArgIndex];
        } else if (Arg == "--out" && HasValue) {
            Options.OutDir = Argv[++ArgIndex];
        } else if (Arg == "--rig" && HasValue) {
            Options.RigName = Argv[++ArgIndex];
        } else if (Arg == "--variant" && HasValue) {
            const std::string_view Name = Argv[++ArgIndex];
            if (Name == "pixel") {
                Options.Variants = {tools::Variant::Pixel};
            } else if (Name == "smooth") {
                Options.Variants = {tools::Variant::Smooth};
            } else if (Name != "all") {
                log::error("unknown variant '{}': expected pixel, smooth or all", Name);
                return EXIT_FAILURE;
            }
        } else if (Arg == "--preview" && HasValue) {
            Options.PreviewPath = Argv[++ArgIndex];
        } else if (Arg == "--overlap" && HasValue) {
            Options.Overlap = std::strtof(Argv[++ArgIndex], nullptr);
        } else {
            log::error("unknown argument: {}", Arg);
            return EXIT_FAILURE;
        }
    }

    try {
        const auto Files = tools::generatePlaceholders(Options);
        log::info("wrote {} files to {}", Files.size(), Options.OutDir.string());
        return EXIT_SUCCESS;
    } catch (const std::exception& Error) {
        log::error("fatal: {}", Error.what());
        return EXIT_FAILURE;
    }
}
