# 🏗️ UML-подобная диаграмма архитектуры Fighter

## 📊 Компактная схема классов

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                           MAIN LOOP                                                      │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │  EventManager                                                                                      │  │
│  │  ├─ mouse_press: MouseEvent                                                                        │  │
│  │  ├─ mouse_release: MouseEvent                                                                      │  │
│  │  ├─ mouse_move: MouseEvent                                                                         │  │
│  │  ├─ key_press: KeyboardEvent                                                                       │  │
│  │  ├─ key_release: KeyboardEvent                                                                     │  │
│  │  ├─ clock: TimeEvent                                                                               │  │
│  │  └─ Create*Handler()                                                                               │  │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    ЯДРО СИСТЕМЫ                                                          │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  GraphicsModule  │  PhysicsModule  │  CoordinateSystem  │  SpriteManager                               │
│  ├─ window       │  ├─ all_objects │  ├─ camera         │  ├─ textures                                  │
│  ├─ screen       │  ├─ spatialGrid │  ├─ worldSettings  │  ├─ loadTexture()                             │
│  ├─ drawingQueue │  ├─ gravity     │  ├─ worldToScreen()│  └─ getTexture()                               │
│  ├─ draw()       │  ├─ updateObjects()│  ├─ screenToWorld()│                                              │
│  └─ insertObject()│  └─ collideObjects()│  └─ followTarget()│                                              │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  FightController  │  Fight  │  Scene  │  GUI                                                             │
│  ├─ physModule    │  ├─ lftPlayer│  ├─ Background│  ├─ (Планируется)                                   │
│  ├─ graphModule   │  ├─ rgtPlayer│  ├─ BaseBlock │  └─ (Планируется)                                   │
│  ├─ currentFight │  ├─ objects   │  └─ (Планируется)│                                                    │
│  └─ startFight() │  └─ restart() │                 │                                                    │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   ИГРОВЫЕ ОБЪЕКТЫ                                                        │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  GameObject  │  Fighter  │  Background  │  BaseBlock  │  HitboxRect                                    │
│  ├─ Множественное│  ├─ hp        │  ├─ Фон        │  ├─ Платформа   │  ├─ Отладка коллизий              │
│  │  наследование │  ├─ sit()     │  ├─ Неподвижный│  ├─ Entity::FIXED│  └─ sf::RectangleShape            │
│  │  Entity +     │  ├─ jump()    │  └─ Фон        │  └─ Неподвижный │                                   │
│  │  DrawableSprite│  ├─ move_left()│                 │                 │                                   │
│  ├─ Автосинхронизация│  ├─ move_right()│                 │                 │                                   │
│  └─ updateGraphicsPosition()│  ├─ attack_hand()│                 │                 │                                   │
│                             │  └─ attack_leg() │                 │                 │                                   │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  Entity  │  DrawableSprite  │  DrawableText  │  Vec2  │  Rect                                          │
│  ├─ State│  ├─ sf::Sprite   │  ├─ sf::Text   │  ├─ x,y│  ├─ x,y,w,h                                   │
│  ├─ pos  │  ├─ setPosition()│  ├─ setText()  │  ├─ +,-│  ├─ getPos()                                   │
│  ├─ vel  │  ├─ setScale()   │  ├─ setSize()  │  ├─ *,/│  ├─ getSize()                                  │
│  ├─ mass │  ├─ setRotation()│  └─ draw()     │  ├─ dot│  ├─ contains()                                 │
│  ├─ friction│  └─ draw()      │                 │  ├─ length│  └─ checkCollision()                        │
│  ├─ elasticity│                 │                 │  └─ angle │                                          │
│  ├─ maxVel│                 │                 │        │                                          │
│  ├─ angularVel│                 │                 │        │                                          │
│  ├─ rotation│                 │                 │        │                                          │
│  └─ hitbox│                 │                 │        │                                          │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## 🔗 Ключевые наследования

### **1. GameObject - Мост между движками**
```cpp
class GameObject : public DrawableSprite, public Entity {
    // Множественное наследование:
    // - DrawableSprite (графика)
    // - Entity (физика)
    
    // Автоматическая синхронизация:
    void updateGraphicsPosition();  // Физика → Графика
    void setWorldPosition();        // Обновление физики + автосинхронизация
    void setWorldScale();           // Обновление физики + автосинхронизация
    void setWorldRotation();        // Обновление физики + автосинхронизация
};
```

### **2. Иерархия Drawable**
```cpp
class Drawable {                    // Абстрактный базовый класс
    virtual void draw() = 0;        // Чисто виртуальная функция
    virtual void setPosition() = 0; // Чисто виртуальная функция
    virtual void setScale() = 0;    // Чисто виртуальная функция
    virtual void setRotation() = 0; // Чисто виртуальная функция
};

class DrawableSprite : public Drawable {  // Конкретная реализация
    sf::Sprite sprite;              // SFML спрайт
    void draw() override;           // Отрисовка спрайта
    void setPosition() override;    // Установка позиции
    void setScale() override;       // Установка масштаба
    void setRotation() override;    // Установка поворота
};

class DrawableText : public Drawable {    // Конкретная реализация
    sf::Text text;                  // SFML текст
    void draw() override;           // Отрисовка текста
    // setRotation() не поддерживается для текста
};
```

### **3. Иерархия Entity**
```cpp
class Entity {                      // Базовый физический объект
    enum State {                    // Состояния объекта
        FIXED, STILL, ON_MOVE,      // Неподвижный, покой, в движении
        IN_AIR, UNSTOPPABLE,        // В воздухе, неостанавливаемый
        SLIDING, ROLLING            // Скольжение, качение
    };
    
    Vec2 position, velocity, scale; // Физические свойства
    double mass, friction, elasticity; // Физические параметры
    double maxVelocity, angularVelocity, rotation; // Дополнительные свойства
    Hitbox hitbox;                  // Система коллизий
};

class Fighter : public Entity {      // Игровой персонаж
    uint8_t hp;                     // Здоровье
    FighterInfo fighterStartParams; // Начальные параметры
    
    // Игровые действия:
    void sit(), jump(), move_left(), move_right();
    void attack_with_hand(), attack_with_leg();
};

class BaseBlock : public Entity {    // Неподвижная платформа
    // Наследует Entity с State::FIXED
    // Неподвижный объект для коллизий
};
```

## 🔄 Поток данных

### **Основной цикл обновления**
```
1. MAIN LOOP (main.cpp)
   ├─ Обработка SFML событий
   ├─ Конвертация в внутренние события
   └─ EventManager::Clock(dt)

2. EventManager распределяет события
   ├─ PhysicsModule::updateObjects(dt)
   ├─ GraphicsModule::TimeEvent(dt)
   └─ FightController::timeEvent(dt)

3. PhysicsModule обновляет физику
   ├─ Применение физики к Entity
   ├─ Проверка коллизий (SpatialGrid)
   └─ Обновление позиций

4. GameObject автоматически синхронизирует
   ├─ updateGraphicsPosition()
   ├─ Конвертация мировых координат в экранные
   └─ Обновление SFML спрайтов

5. GraphicsModule отрисовывает
   ├─ Отрисовка всех Drawable объектов
   └─ Обновление экрана
```

### **Связи между модулями**
```
EventManager ←→ PhysicsModule (временные события)
EventManager ←→ GraphicsModule (временные события)
EventManager ←→ FightController (все события)

PhysicsModule ←→ Entity (физика объектов)
PhysicsModule ←→ SpatialGrid (оптимизация коллизий)

GraphicsModule ←→ Drawable (отрисовка)
GraphicsModule ←→ SpriteManager (ресурсы)

FightController ←→ PhysicsModule (управление физикой)
FightController ←→ GraphicsModule (управление графикой)

GameObject ←→ Entity + DrawableSprite (мост между движками)
CoordinateSystem ←→ GameObject (конвертация координат)
```

## 📊 Статистика архитектуры

- **Всего классов**: 25+
- **Уровней наследования**: 3-4
- **Интерфейсов**: 5 (абстрактных классов)
- **Модулей**: 8 основных
- **Связей между модулями**: 15+
- **Паттерны**: Observer (события), Bridge (GameObject), Factory

## 🎯 Ключевые принципы

1. **Разделение ответственности** - каждый модуль за свою область
2. **Слабая связанность** - взаимодействие через интерфейсы
3. **Высокая когезия** - связанные функции в классах
4. **Расширяемость** - легко добавлять новую функциональность
5. **Производительность** - оптимизированные алгоритмы
6. **Тестируемость** - модульная структура

## 🚀 Преимущества

✅ **Модульность** - независимые компоненты
✅ **Масштабируемость** - легко расширять
✅ **Производительность** - оптимизированные алгоритмы
✅ **Читаемость** - четкая структура
✅ **Поддерживаемость** - простое внесение изменений
✅ **Переиспользование** - компоненты для других проектов
