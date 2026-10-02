#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "core/signal.hpp"

using fighter::Connection;
using fighter::Signal;

TEST_CASE("Signal: handler is called while the connection is alive", "[core][signal]") {
    Signal<int> Sig;
    int Sum = 0;
    {
        Connection Conn = Sig.connect([&](int Value) { Sum += Value; });
        Sig.emit(5);
        CHECK(Sum == 5);
        CHECK(Sig.getHandlerCount() == 1);
    }
    Sig.emit(5);   // the connection is destroyed, the handler is not called
    CHECK(Sum == 5);
    CHECK(Sig.getHandlerCount() == 0);
}

TEST_CASE("Signal: moving a connection keeps it connected", "[core][signal]") {
    Signal<> Sig;
    int Calls = 0;
    Connection Outer;
    {
        Connection Inner = Sig.connect([&] { ++Calls; });
        Outer = std::move(Inner);
    }
    Sig.emit();
    CHECK(Calls == 1);
    CHECK(Outer.isConnected());
    Outer.disconnect();
    Sig.emit();
    CHECK(Calls == 1);
}

TEST_CASE("Signal: disconnect from inside the handler", "[core][signal]") {
    Signal<> Sig;
    int Calls = 0;
    std::optional<Connection> Conn;
    Conn = Sig.connect([&] {
        ++Calls;
        Conn->disconnect();
    });
    Sig.emit();
    Sig.emit();
    CHECK(Calls == 1);
}

TEST_CASE("Signal: handler added during emit fires from the next emit", "[core][signal]") {
    Signal<> Sig;
    int Late = 0;
    std::optional<Connection> Added;
    Connection First = Sig.connect([&] {
        if (!Added) Added = Sig.connect([&] { ++Late; });
    });
    Sig.emit();
    CHECK(Late == 0);
    Sig.emit();
    CHECK(Late == 1);
}

TEST_CASE("Signal: connection safely outlives the signal", "[core][signal]") {
    Connection Conn;
    {
        Signal<> Sig;
        Conn = Sig.connect([] {});
    }
    CHECK_FALSE(Conn.isConnected());
    Conn.disconnect();   // does not crash
}
