#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// Сигналы с RAII-подпиской — замена старого EventManager.
//
//     Signal<int> damaged;
//     Connection c = damaged.connect([](int hp) { ... });
//     damaged.emit(10);   // вызовет обработчик
//     // c уничтожен → обработчик отписан автоматически
//
// В отличие от старой системы, подписчик не может «повиснуть»: если он умер,
// вместе с ним умерла и подписка. Подписываться и отписываться можно прямо из обработчика.
namespace fighter {

namespace detail {
struct SlotState {
    bool connected = true;
};
} // namespace detail

// Владеющий дескриптор подписки. Только перемещается. Отписывает в деструкторе.
class Connection {
public:
    Connection() = default;
    explicit Connection(std::weak_ptr<detail::SlotState> slot) : slot_(std::move(slot)) {}

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept : slot_(std::exchange(other.slot_, {})) {}
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            disconnect();
            slot_ = std::exchange(other.slot_, {});
        }
        return *this;
    }

    ~Connection() { disconnect(); }

    void disconnect() {
        if (auto s = slot_.lock()) s->connected = false;
        slot_.reset();
    }

    bool connected() const {
        auto s = slot_.lock();
        return s && s->connected;
    }

private:
    std::weak_ptr<detail::SlotState> slot_;
};

template <class... Args>
class Signal {
public:
    using Handler = std::function<void(Args...)>;

    Signal() = default;
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    [[nodiscard]] Connection connect(Handler handler) {
        auto slot = std::make_shared<Slot>();
        slot->handler = std::move(handler);
        slots_.push_back(slot);
        return Connection(slot);
    }

    void emit(Args... args) {
        ++emitDepth_;
        // Обработчик может добавить новые слоты: идём по индексу и только по тем,
        // что были на момент вызова. shared_ptr держит слот живым во время вызова.
        const std::size_t count = slots_.size();
        for (std::size_t i = 0; i < count; ++i) {
            std::shared_ptr<Slot> slot = slots_[i];
            if (slot->connected) slot->handler(args...);
        }
        --emitDepth_;
        if (emitDepth_ == 0) removeDisconnected();
    }

    std::size_t handlerCount() const {
        return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(),
                                                      [](const auto& s) { return s->connected; }));
    }

private:
    struct Slot : detail::SlotState {
        Handler handler;
    };

    void removeDisconnected() {
        std::erase_if(slots_, [](const auto& s) { return !s->connected; });
    }

    std::vector<std::shared_ptr<Slot>> slots_;
    int emitDepth_ = 0;
};

} // namespace fighter
