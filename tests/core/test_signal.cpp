#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "core/signal.hpp"

using fighter::Connection;
using fighter::Signal;

TEST_CASE("Signal: handler is called while the connection is alive", "[core][signal]") {
    Signal<int> S;
    int Sum = 0;
    {
        Connection C = S.connect([&](int V) { Sum += V; });
        S.emit(5);
        CHECK(Sum == 5);
        CHECK(S.getHandlerCount() == 1);
    }
    S.emit(5);   // the connection is destroyed, the handler is not called
    CHECK(Sum == 5);
    CHECK(S.getHandlerCount() == 0);
}

TEST_CASE("Signal: moving a connection keeps it connected", "[core][signal]") {
    Signal<> S;
    int Calls = 0;
    Connection Outer;
    {
        Connection Inner = S.connect([&] { ++Calls; });
        Outer = std::move(Inner);
    }
    S.emit();
    CHECK(Calls == 1);
    CHECK(Outer.isConnected());
    Outer.disconnect();
    S.emit();
    CHECK(Calls == 1);
}

TEST_CASE("Signal: disconnect from inside the handler", "[core][signal]") {
    Signal<> S;
    int Calls = 0;
    std::optional<Connection> C;
    C = S.connect([&] {
        ++Calls;
        C->disconnect();
    });
    S.emit();
    S.emit();
    CHECK(Calls == 1);
}

TEST_CASE("Signal: handler added during emit fires from the next emit", "[core][signal]") {
    Signal<> S;
    int Late = 0;
    std::optional<Connection> Added;
    Connection First = S.connect([&] {
        if (!Added) Added = S.connect([&] { ++Late; });
    });
    S.emit();
    CHECK(Late == 0);
    S.emit();
    CHECK(Late == 1);
}

TEST_CASE("Signal: connection safely outlives the signal", "[core][signal]") {
    Connection C;
    {
        Signal<> S;
        C = S.connect([] {});
    }
    CHECK_FALSE(C.isConnected());
    C.disconnect();   // does not crash
}
