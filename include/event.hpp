#pragma <once>

#include <iostream>
#include <list>
#include "keyboard.hpp"
#include "mouse.hpp"

template <typename... Args>
class EventHandlerBase {
    public:
    virtual void call(Args... args) = 0;
    virtual bool equals(const EventHandlerBase<Args...>* other) const = 0;
    virtual ~EventHandlerBase() = default;
};

template <typename T, typename... Args>
class EventHandler : public EventHandlerBase<Args...> {
    using obj_t = T;
    using method_t = void(obj_t::*)(Args...);
    using base_handler_t = EventHandlerBase<Args...>;
    using handler_t = EventHandler<T, Args...>;

    public:
    explicit EventHandler(obj_t& obj_, method_t method_):
        obj (obj_),
        method (method_) {}
    
    virtual ~EventHandler() = default;

    virtual void call(Args... args) override {
        (obj.*method)(args...);
    }

    virtual bool equals(const base_handler_t* other_) const override {
        const handler_t* other = dynamic_cast<const handler_t*>(other_);
        if (other == nullptr) return false;
        return (&obj == &(other -> obj) && method == other -> method);
    }

    private:
    obj_t& obj;
    method_t method;
};

template <typename... Args>
class Event {
    using handler_t = EventHandlerBase<Args...>;
    public:

    explicit Event() = default;
    
    ~Event() {
        for (auto handler : handlers) delete handler;
    }

    void operator()(Args... args) {
        for (handler_t* hand : handlers)
            hand -> call(args...);
    }

    void addHandler(handler_t* hand) {
        handlers.push_back(hand);
    }

    void removeHandler(handler_t* hand) {
        handlers.remove_if([hand](const handler_t* other) {return hand -> equals(other);});
    }

    private:
    std::list<handler_t*> handlers;
};

using MouseEvent = Event<MouseContext>;
using KeyboardEvent = Event<KeyboardContext>;
using TimeEvent = Event<double>;


class EventManager {
    public:
    explicit EventManager():
        enabled(true) {}

    ~EventManager() = default;

    void enable () {enabled = true; }
    void disable() {enabled = false;}

    void mousePress(MouseContext context) {
        if (enabled) mouse_press(context);
    }

    void mouseRelease(MouseContext context) {
        if (enabled) mouse_release(context);
    }

    void mouseMove(MouseContext context) {
        if (enabled) mouse_move(context);
    }

    void keyPress(KeyboardContext context) {
        if (enabled) key_press(context);
    }

    void keyRelease(KeyboardContext context) {
        if (enabled) key_release(context);
    }

    void Clock(double dt) {
        if (enabled) clock(dt);
    }

    template<typename T>
    void CreateMousePressHandler(T& obj, void(T::*method)(MouseContext)) {
        mouse_press.addHandler(new EventHandler<T, MouseContext>(obj, method));
    }

    template<typename T>
    void CreateMouseReleaseHandler(T& obj, void(T::*method)(MouseContext)) {
        mouse_release.addHandler(new EventHandler<T, MouseContext>(obj, method));
    }

    template<typename T>
    void CreateMouseMoveHandler(T& obj, void(T::*method)(MouseContext)) {
        mouse_move.addHandler(new EventHandler<T, MouseContext>(obj, method));
    }

    template<typename T>
    void CreateKeyPressHandler(T& obj, void(T::*method)(KeyboardContext)) {
        key_press.addHandler(new EventHandler<T, KeyboardContext>(obj, method));
    }

    template<typename T>
    void CreateKeyReleaseHandler(T& obj, void(T::*method)(KeyboardContext)) {
        key_release.addHandler(new EventHandler<T, KeyboardContext>(obj, method));
    }

    template<typename T>
    void CreateClockHandler(T& obj, void(T::*method)(double)) {
        clock.addHandler(new EventHandler<T, double>(obj, method));
    }

    void addSubManager(EventManager& other) {
        CreateMousePressHandler  (other, &EventManager::mousePress  );
        CreateMouseReleaseHandler(other, &EventManager::mouseRelease);
        CreateKeyPressHandler    (other, &EventManager::keyPress    );
        CreateKeyReleaseHandler  (other, &EventManager::keyRelease  );
        CreateClockHandler       (other, &EventManager::Clock       );
    }

    private:
    
    bool enabled;
    MouseEvent mouse_press;
    MouseEvent mouse_release;
    MouseEvent mouse_move;
    KeyboardEvent key_press;
    KeyboardEvent key_release;
    TimeEvent clock;
};
