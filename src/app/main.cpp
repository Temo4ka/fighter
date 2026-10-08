#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

#include "app/app.hpp"
#include "app/check_data.hpp"
#include "combat/move_measure.hpp"
#include "core/log.hpp"

// Command line arguments:
//   --screenshot <file.png>     save a frame and exit (verification, bug reports)
//   --frames <N>                frame on which the screenshot is taken (default 60)
//   --mode debug|both|textures  initial mode of the debug layer (debug build)
//   --showcase                  samples of every debug category (debug build)
//   --style <name>              a style of data/visuals.json "styles" (smooth, pixel)
//   --demo walk|fight|kick      scripted input instead of the keyboard
//   --left <fighter>            fighter sheet of P1 (data/fighters/<fighter>.json), default: built-in
//   --right <fighter>           the same for P2
//   --menu                      start in the main menu even with the flags above
//   --keys <list>               menu keys, one per frame, e.g. enter,down,esc (screenshots)
//   --round <sec>               round time (reach the results screen quickly)
//   --log <file>                duplicate the log to a file
//   --stand <move>              the move stand: one fighter repeats the move in front of a dummy
//   --weapon <item>             with --stand: the item (data/items/) held in the main hand
//   --check-data                check every file of data/, print the problems, exit 0 if none
//
// Without any of the battle flags (--left, --right, --demo, --mode, --showcase,
// --frames, --screenshot) the app starts in the main menu; with one of them it
// starts straight in a battle, as the sandbox always did.
//
// Project root (assets/, data/): the FIGHTER_ROOT environment variable,
// otherwise the source directory known at build time.
int main(int Argc, char** Argv) {
    using namespace fighter;

    app::Options Opts;
    const char* RootEnv = std::getenv("FIGHTER_ROOT");
    Opts.Root = RootEnv ? RootEnv : FIGHTER_SOURCE_DIR;

    bool BattleFlag = false;
    bool MenuFlag = false;
    bool CheckData = false;
    for (int ArgIndex = 1; ArgIndex < Argc; ++ArgIndex) {
        const std::string_view Arg = Argv[ArgIndex];
        const bool HasValue = ArgIndex + 1 < Argc;
        if (Arg == "--keys" && HasValue) {
            Opts.Keys = app::parseMenuKeys(Argv[++ArgIndex]);
            continue;
        }
        if (Arg == "--round" && HasValue) {
            Opts.RoundSec = std::atof(Argv[++ArgIndex]);
            continue;
        }
        if (Arg == "--stand" && HasValue) {
            Opts.StandMove = Argv[++ArgIndex];
            BattleFlag = true;
            continue;
        }
        if (Arg == "--weapon" && HasValue) {
            Opts.StandWeapon = Argv[++ArgIndex];
            continue;
        }
        if (Arg == "--check-data") {
            CheckData = true;
            continue;
        }
        if (Arg == "--menu") {
            MenuFlag = true;
            continue;
        }
        if (Arg != "--log") BattleFlag = true;
        if (Arg == "--screenshot" && HasValue) {
            Opts.Screenshot = Argv[++ArgIndex];
        } else if (Arg == "--frames" && HasValue) {
            Opts.Frames = std::atoi(Argv[++ArgIndex]);
        } else if (Arg == "--mode" && HasValue) {
            Opts.Mode = Argv[++ArgIndex];
        } else if (Arg == "--style" && HasValue) {
            Opts.Style = Argv[++ArgIndex];
        } else if (Arg == "--demo" && HasValue) {
            Opts.Demo = app::findDemoScript(Argv[++ArgIndex]);
            if (!Opts.Demo) log::warn("unknown demo script: {}", Argv[ArgIndex]);
        } else if (Arg == "--left" && HasValue) {
            Opts.LeftFighter = Argv[++ArgIndex];
        } else if (Arg == "--right" && HasValue) {
            Opts.RightFighter = Argv[++ArgIndex];
        } else if (Arg == "--showcase") {
            Opts.Showcase = true;
        } else if (Arg == "--log" && HasValue) {
            if (!log::setFile(Argv[++ArgIndex])) log::warn("cannot open log file {}", Argv[ArgIndex]);
        } else {
            log::warn("unknown argument: {}", Arg);
        }
    }

    if (CheckData) return app::runDataCheck(Opts.Root, std::cout);

    if (!Opts.StandWeapon.empty() && !Opts.StandMove) log::warn("--weapon is used with --stand only");
    if (Opts.StandMove) {
        // Unknown move or item: say so before any window opens.
        try {
            combat::prepareStand({.MoveId = *Opts.StandMove, .WeaponId = Opts.StandWeapon,
                                  .WithDummy = true, .DataDir = Opts.Root / "data"});
        } catch (const std::exception& Error) {
            log::error("cannot start the stand: {}", Error.what());
            return EXIT_FAILURE;
        }
    }

    Opts.Menu = MenuFlag || !BattleFlag;

    try {
        app::App Application(std::move(Opts));
        return Application.run();
    } catch (const std::exception& Error) {
        log::error("fatal: {}", Error.what());
        return EXIT_FAILURE;
    }
}
