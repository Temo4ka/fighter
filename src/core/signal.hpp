//===- core/signal.hpp - Signals with RAII connections ----------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines Signal, a list of callbacks invoked by emit(), and
/// Connection, the owning handle that unsubscribes a callback when destroyed.
///
/// A subscriber cannot dangle: when it dies, its Connection dies with it and
/// the callback is removed. Handlers may connect and disconnect from inside
/// emit().
///
/// \code
///   Signal<int> Damaged;
///   Connection C = Damaged.connect([](int Hp) { ... });
///   Damaged.emit(10);   // calls the handler
///   // C destroyed -> handler disconnected automatically
/// \endcode
///
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace fighter {

namespace detail {
struct SlotState {
    bool Connected = true;
};
} // namespace detail

/// Owning subscription handle. Move-only; disconnects in the destructor.
class Connection {
public:
    Connection() = default;
    explicit Connection(std::weak_ptr<detail::SlotState> State) : Slot(std::move(State)) {}

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& Other) noexcept : Slot(std::exchange(Other.Slot, {})) {}
    Connection& operator=(Connection&& Other) noexcept {
        if (this != &Other) {
            disconnect();
            Slot = std::exchange(Other.Slot, {});
        }
        return *this;
    }

    ~Connection() { disconnect(); }

    void disconnect() {
        if (auto S = Slot.lock()) S->Connected = false;
        Slot.reset();
    }

    bool isConnected() const {
        auto S = Slot.lock();
        return S && S->Connected;
    }

private:
    std::weak_ptr<detail::SlotState> Slot;
};

template <class... Args>
class Signal {
public:
    using Handler = std::function<void(Args...)>;

    Signal() = default;
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    [[nodiscard]] Connection connect(Handler Callback) {
        auto Slot = std::make_shared<SlotData>();
        Slot->Callback = std::move(Callback);
        Slots.push_back(Slot);
        return Connection(Slot);
    }

    void emit(Args... Arguments) {
        ++EmitDepth;
        // A handler may add new slots, so iterate by index over the slots that
        // existed when emit() started. The shared_ptr copy keeps the slot alive
        // for the duration of the call.
        const std::size_t Count = Slots.size();
        for (std::size_t I = 0; I < Count; ++I) {
            std::shared_ptr<SlotData> Slot = Slots[I];
            if (Slot->Connected) Slot->Callback(Arguments...);
        }
        --EmitDepth;
        if (EmitDepth == 0) removeDisconnected();
    }

    std::size_t getHandlerCount() const {
        return static_cast<std::size_t>(std::count_if(Slots.begin(), Slots.end(),
                                                      [](const auto& S) { return S->Connected; }));
    }

private:
    struct SlotData : detail::SlotState {
        Handler Callback;
    };

    void removeDisconnected() {
        std::erase_if(Slots, [](const auto& S) { return !S->Connected; });
    }

    std::vector<std::shared_ptr<SlotData>> Slots;
    int EmitDepth = 0;
};

} // namespace fighter
