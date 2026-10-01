#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "core/signal.hpp"

using fighter::Connection;
using fighter::Signal;

TEST_CASE("Signal: handler is called while the connection is alive", "[core][signal]") {
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

TEST_CASE("Signal: moving a connection keeps it connected", "[core][signal]") {
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

TEST_CASE("Signal: disconnect from inside the handler", "[core][signal]") {
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

TEST_CASE("Signal: handler added during emit fires from the next emit", "[core][signal]") {
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

TEST_CASE("Signal: connection safely outlives the signal", "[core][signal]") {
    Connection c;
    {
        Signal<> s;
        c = s.connect([] {});
    }
    CHECK_FALSE(c.connected());
    c.disconnect();   // не падает
}
