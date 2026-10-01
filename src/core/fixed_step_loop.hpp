#pragma once

#include <concepts>
#include <cstdint>

namespace fighter {

// Фиксированный шаг симуляции с аккумулятором (docs/DEVELOPMENT_PLAN.md §3.2).
//
// Реальное время кадра копится в аккумуляторе и «расходуется» шагами одинаковой длины.
// Физика всегда получает один и тот же dt — поведение не зависит от FPS и воспроизводимо.
//
//     double alpha = loop.advance(frameSec, [&](double dt) { battle.update(..., dt); });
//     renderer.draw(prev, curr, alpha);
class FixedStepLoop {
public:
    struct Config {
        double stepSec = 1.0 / 60.0;
        // Кадр длиннее этого (отладчик, перетаскивание окна) обрезается, иначе симуляция
        // будет догонять время сотнями шагов подряд («спираль смерти»).
        double maxFrameSec = 0.25;
    };

    static constexpr double kMinTimeScale = 0.1;
    static constexpr double kMaxTimeScale = 1.0;

    FixedStepLoop() : FixedStepLoop(Config{}) {}
    explicit FixedStepLoop(Config config);

    // Продвигает время на frameSec секунд реального времени и вызывает step(dt)
    // нужное число раз. Возвращает alpha ∈ [0, 1] — насколько отрисовка должна
    // продвинуться от предыдущего состояния к текущему.
    template <std::invocable<double> Step>
    double advance(double frameSec, Step&& step) {
        stepsLastAdvance_ = 0;

        if (paused_) {
            if (singleStepRequested_) {
                singleStepRequested_ = false;
                runStep(step);
            }
            return 1.0;   // на паузе показываем ровно текущее состояние
        }

        accumulator_ += clampFrame(frameSec) * timeScale_;
        while (accumulator_ >= config_.stepSec) {
            runStep(step);
            accumulator_ -= config_.stepSec;
        }
        return accumulator_ / config_.stepSec;
    }

    // --- Управление временем (используется debug-сборкой) ---
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    // На паузе: выполнить ровно один шаг при следующем advance().
    void requestSingleStep() { singleStepRequested_ = true; }
    // Замедление: dt шага не меняется, меняется только темп реального времени.
    void setTimeScale(double scale);
    double timeScale() const { return timeScale_; }

    double stepSec() const { return config_.stepSec; }
    std::uint64_t tick() const { return tick_; }
    int stepsLastAdvance() const { return stepsLastAdvance_; }

    // Сбросить счётчик шагов и накопленное время (перезапуск боя).
    void reset();

private:
    template <class Step>
    void runStep(Step& step) {
        step(config_.stepSec);
        ++tick_;
        ++stepsLastAdvance_;
    }

    double clampFrame(double frameSec) const;

    Config config_;
    double accumulator_ = 0.0;
    double timeScale_ = 1.0;
    bool paused_ = false;
    bool singleStepRequested_ = false;
    std::uint64_t tick_ = 0;
    int stepsLastAdvance_ = 0;
};

} // namespace fighter
