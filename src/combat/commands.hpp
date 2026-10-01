#pragma once

// Ввод игрока на один шаг симуляции (docs/DEVELOPMENT_PLAN.md §4).
// Это состояние кнопок, а не события: «удерживается ли сейчас». Одинаково
// заполняется клавиатурой, геймпадом, ИИ или записью реплея.
namespace fighter::combat {

struct PlayerCommands {
    float moveX = 0.0f;   // -1 … 1
    bool jump = false;
    bool crouch = false;
    bool block = false;
    bool punch = false;
    bool kick = false;

    constexpr bool operator==(const PlayerCommands&) const = default;
};

} // namespace fighter::combat
