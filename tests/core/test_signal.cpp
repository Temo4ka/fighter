#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "core/signal.hpp"

using fighter::Connection;
using fighter::Signal;

TEST_CASE("Signal: обработчик вызывается, пока жива подписка", "[core][signal]") {
    Signal<int> s;
    int sum = 0;
    {
        Connection c = s.connect([&](int v) { sum += v; });
        s.emit(5);
        CHECK(sum == 5);
        CHECK(s.handlerCount() == 1);
    }
    s.emit(5);   // подписка уничтожена — обработчик не вызывается
    CHECK(sum == 5);
    CHECK(s.handlerCount() == 0);
}

TEST_CASE("Signal: перемещение подписки не отписывает", "[core][signal]") {
    Signal<> s;
    int calls = 0;
    Connection outer;
    {
        Connection inner = s.connect([&] { ++calls; });
        outer = std::move(inner);
    }
    s.emit();
    CHECK(calls == 1);
    CHECK(outer.connected());
    outer.disconnect();
    s.emit();
    CHECK(calls == 1);
}

TEST_CASE("Signal: отписка изнутри обработчика", "[core][signal]") {
    Signal<> s;
    int calls = 0;
    std::optional<Connection> c;
    c = s.connect([&] {
        ++calls;
        c->disconnect();
    });
    s.emit();
    s.emit();
    CHECK(calls == 1);
}

TEST_CASE("Signal: подписка, добавленная во время emit, срабатывает со следующего раза", "[core][signal]") {
    Signal<> s;
    int late = 0;
    std::optional<Connection> added;
    Connection first = s.connect([&] {
        if (!added) added = s.connect([&] { ++late; });
    });
    s.emit();
    CHECK(late == 0);
    s.emit();
    CHECK(late == 1);
}

TEST_CASE("Signal: подписка переживает сигнал без ошибок", "[core][signal]") {
    Connection c;
    {
        Signal<> s;
        c = s.connect([] {});
    }
    CHECK_FALSE(c.connected());
    c.disconnect();   // не падает
}
