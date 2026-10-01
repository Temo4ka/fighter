#include <cstdlib>
#include <exception>
#include <string_view>

#include "app/app.hpp"
#include "core/log.hpp"

// Command line arguments:
//   --screenshot <file.png>     save a frame and exit (verification, bug reports)
//   --frames <N>                frame on which the screenshot is taken (default 60)
//   --mode debug|both|textures  initial mode of the debug layer (debug build)
//   --showcase                  samples of every debug category (debug build)
//   --log <file>                duplicate the log to a file
//
// Project root (assets/, data/): the FIGHTER_ROOT environment variable,
// otherwise the source directory known at build time.
int main(int Argc, char** Argv) {
    using namespace fighter;

    app::Options Opts;
    const char* RootEnv = std::getenv("FIGHTER_ROOT");
    Opts.Root = RootEnv ? RootEnv : FIGHTER_SOURCE_DIR;

    for (int ArgIndex = 1; ArgIndex < Argc; ++ArgIndex) {
        const std::string_view Arg = Argv[ArgIndex];
        const bool HasValue = ArgIndex + 1 < Argc;
        if (Arg == "--screenshot" && HasValue) {
            Opts.Screenshot = Argv[++ArgIndex];
        } else if (Arg == "--frames" && HasValue) {
            Opts.Frames = std::atoi(Argv[++ArgIndex]);
        } else if (Arg == "--mode" && HasValue) {
            Opts.Mode = Argv[++ArgIndex];
        } else if (Arg == "--showcase") {
            Opts.Showcase = true;
        } else if (Arg == "--log" && HasValue) {
            if (!log::setFile(Argv[++ArgIndex])) log::warn("cannot open log file {}", Argv[ArgIndex]);
        } else {
            log::warn("unknown argument: {}", Arg);
        }
    }

    try {
        app::App Application(std::move(Opts));
        return Application.run();
    } catch (const std::exception& Error) {
        log::error("fatal: {}", Error.what());
        return EXIT_FAILURE;
    }
}
