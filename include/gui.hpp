#pragma <once>

#include <cstdint>
#include <vector>

#include "graphics.hpp"

class Widget : public Drawable {
    // explicit Widget(SpriteInfo info, Vec2 pos_, Vec2 size_) {}

    // virtual bool onMousePress  (const MouseContext mouseContext);
    // virtual void onMouseRelease(const MouseContext mouseContext);
    // virtual bool onMouseMove   (const MouseContext mouseContext);

    // void  pushBackSubWidget(Widget *subWidget);
    // void pushFrontSubWidget(Widget *subWidget);

    // std::list<Widget*>& getList();
    // const std::list<Widget*>& getList() const;
    // bool mouseOnWidget (const Vec2& mousepos) const;

    // virtual ~Widget();

    // virtual ObjType getType() const override {return WIDGET;}
    // virtual void load(std::ifstream &stream) {}
    // virtual void save(std::ofstream &stream) const {};

    // private:

    // bool passMousePress  (const MouseContext mouseContext);
    // void passMouseRelease(const MouseContext mouseContext);
    // bool passMouseMove   (const MouseContext mouseContext);

    // std::list<Widget*> subWidgets;
};

class Button : public Widget {

};

using Menu = std::vector<Button>;