#include <cstdlib>
#include <exception>
#include <string_view>

#include "app/app.hpp"
#include "core/log.hpp"

// Аргументы командной строки:
//   --screenshot <file.png>   сохранить кадр и выйти (проверка и баг-репорты)
//   --frames <N>              через сколько кадров делать снимок (по умолчанию 60)
//   --mode debug|both|textures  стартовый режим отладочного слоя (debug-сборка)
//   --showcase                показать образцы всех отладочных категорий (debug-сборка)
//   --log <file>              дублировать лог в файл
//
// Корень проекта (assets/, data/): переменная окружения FIGHTER_ROOT,
// иначе каталог исходников, известный на этапе сборки.
int main(int argc, char** argv) {
    using namespace fighter;

    app::Options options;
    const char* rootEnv = std::getenv("FIGHTER_ROOT");
    options.root = rootEnv ? rootEnv : FIGHTER_SOURCE_DIR;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--screenshot" && hasValue) {
            options.screenshot = argv[++i];
        } else if (arg == "--frames" && hasValue) {
            options.frames = std::atoi(argv[++i]);
        } else if (arg == "--mode" && hasValue) {
            options.mode = argv[++i];
        } else if (arg == "--showcase") {
            options.showcase = true;
        } else if (arg == "--log" && hasValue) {
            if (!log::setFile(argv[++i])) log::warn("cannot open log file {}", argv[i]);
        } else {
            log::warn("unknown argument: {}", arg);
        }
    }

    try {
        app::App application(std::move(options));
        return application.run();
    } catch (const std::exception& e) {
        log::error("fatal: {}", e.what());
        return EXIT_FAILURE;
    }
}
