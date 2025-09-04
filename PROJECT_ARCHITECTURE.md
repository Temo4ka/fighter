# 🏗️ Архитектура проекта Fighter

## 📊 Общая схема архитектуры

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                           MAIN LOOP                                                      │
│  ┌─────────────────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │  EventManager (Центральный диспетчер событий)                                                      │  │
│  │  ├─ mousePress() / mouseRelease() / mouseMove()                                                    │  │
│  │  ├─ keyPress() / keyRelease()                                                                      │  │
│  │  └─ Clock() (временные события)                                                                    │  │
│  └─────────────────────────────────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    ЯДРО СИСТЕМЫ                                                          │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  GraphicsModule  │  PhysicsModule  │  CoordinateSystem  │  SpriteManager                               │
│  (Рендеринг)     │  (Физика)       │  (Координаты)      │  (Ресурсы)                                   │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  FightController  │  Fight  │  Scene  │  GUI                                                             │
│  (Игровая логика) │ (Бой)   │ (Уровень)│ (Интерфейс)                                                      │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   ИГРОВЫЕ ОБЪЕКТЫ                                                        │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  GameObject  │  Fighter  │  Background  │  BaseBlock  │  HitboxRect                                    │
│  (Базовый)   │ (Игрок)   │  (Фон)       │ (Платформа) │ (Отладка)                                      │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  Entity  │  DrawableSprite  │  DrawableText  │  Vec2  │  Rect                                          │
│ (Физика) │   (Графика)      │   (Текст)      │ (Матем)│ (Геометрия)                                   │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## 🔗 Детальная диаграмма наследования

### **1. Иерархия Drawable (Графика)**

```
                    Drawable (Абстрактный базовый класс)
                    ├─ setVisible(bool)
                    ├─ setPosition(sf::Vector2f)
                    ├─ setScale(sf::Vector2f)
                    ├─ setRotation(float)
                    └─ draw(sf::RenderTexture&)
                                    │
                    ┌───────────────┼───────────────┐
                    │               │               │
            DrawableSprite    DrawableText    HitboxRect
            ├─ sf::Sprite     ├─ sf::Text      ├─ sf::RectangleShape
            ├─ setPosition()  ├─ setTextString()├─ setPosition()
            ├─ setScale()     ├─ setTextSize() ├─ setScale()
            ├─ setRotation()  └─ draw()        └─ draw()
            └─ draw()
                    │
                    ▼
                GameObject
                ├─ updateGraphicsPosition()
                ├─ setWorldPosition()
                ├─ setWorldScale()
                └─ setWorldRotation()
```

### **2. Иерархия Entity (Физика)**

```
                    Entity (Базовый физический объект)
                    ├─ State (FIXED, STILL, ON_MOVE, IN_AIR, UNSTOPPABLE, SLIDING, ROLLING)
                    ├─ position: Vec2
                    ├─ velocity: Vec2
                    ├─ scale: Vec2
                    ├─ mass: double
                    ├─ friction: double
                    ├─ airResistance: double
                    ├─ elasticity: double
                    ├─ maxVelocity: double
                    ├─ angularVelocity: double
                    ├─ rotation: double
                    ├─ hitbox: Hitbox
                    └─ setState(), getState()
                                    │
                    ┌───────────────┼───────────────┐
                    │               │               │
                GameObject      Fighter        BaseBlock
                ├─ Множественное  ├─ hp: uint8_t  ├─ Неподвижная платформа
                │  наследование   ├─ sit()        └─ Entity::FIXED
                │  от Entity      ├─ jump()
                │  и DrawableSprite├─ move_left()
                └─ Автосинхронизация├─ move_right()
                                   ├─ attack_with_hand()
                                   └─ attack_with_leg()
```

### **3. Система координат**

```
                CoordinateSystem (Глобальная система координат)
                ├─ Camera (position, zoom, rotation, target)
                ├─ WorldSettings (worldScale, worldBounds, gravity, wrapWorld)
                ├─ worldToScreen(Vec2) → sf::Vector2f
                ├─ screenToWorld(sf::Vector2f) → Vec2
                ├─ worldToUI(Vec2) → sf::Vector2f
                ├─ setCameraPosition(), setCameraZoom(), setCameraRotation()
                ├─ followTarget(), updateCamera()
                └─ isInScreen(), isInWorld(), clampToWorld()

                Глобальный экземпляр: g_coordSystem
                Утилитарные функции:
                ├─ toScreen(Vec2) → sf::Vector2f
                ├─ toWorld(sf::Vector2f) → Vec2
                ├─ toScreenScale(double) → double
                └─ toWorldScale(double) → double
```

### **4. Система событий**

```
                EventHandlerBase<Args...> (Абстрактный базовый класс)
                ├─ call(Args...)
                └─ equals(const EventHandlerBase*)
                                    │
                            EventHandler<T, Args...>
                            ├─ obj: T&
                            ├─ method: void(T::*)(Args...)
                            ├─ call(Args...)
                            └─ equals(const EventHandlerBase*)

                Event<Args...> (Контейнер обработчиков)
                ├─ addHandler(EventHandlerBase*)
                ├─ removeHandler(EventHandlerBase*)
                └─ operator()(Args...)

                Специализации:
                ├─ MouseEvent = Event<MouseContext>
                ├─ KeyboardEvent = Event<KeyboardContext>
                └─ TimeEvent = Event<Time_t>

                EventManager (Центральный диспетчер)
                ├─ mouse_press, mouse_release, mouse_move
                ├─ key_press, key_release
                ├─ clock
                ├─ CreateMousePressHandler(), CreateKeyPressHandler(), CreateClockHandler()
                └─ addSubManager()
```

### **5. Физический движок**

```
                PhysicsModule (Основной физический движок)
                ├─ all_objects: vector<Entity*>
                ├─ spatialGrid: SpatialGrid
                ├─ gravity: Vec2
                ├─ globalFriction, globalAirResistance
                ├─ advancedPhysics: bool
                ├─ collisionIterations: int
                ├─ addObject(Entity*), eraseObject(Entity*)
                ├─ updateObjects(Time_t dt)
                ├─ collideObjects()
                ├─ handleCollision(Entity*, Entity*)
                ├─ resolveCollision(Entity*, Entity*)
                ├─ elasticCollision(Entity*, Entity*)
                ├─ applyImpulse(Entity*, Vec2)
                ├─ applyAngularImpulse(Entity*, double)
                └─ applyConstraints(Entity*)

                SpatialGrid (Пространственное разделение)
                ├─ cellSize: double
                ├─ grid: unordered_map<string, vector<Entity*>>
                ├─ addEntity(), removeEntity(), updateEntity()
                ├─ getNearbyEntities()
                └─ getCellKey(), getCellKeys()
```

### **6. Графический движок**

```
                GraphicsModule (Основной графический движок)
                ├─ window: sf::RenderWindow
                ├─ screen: sf::RenderTexture
                ├─ drawingQueue: vector<Drawable*>
                ├─ sprite_man: SpriteManager
                ├─ insertObject(Drawable*), eraseObject(Drawable*)
                ├─ draw()
                ├─ TimeEvent(Time_t dt)
                ├─ windowPollEvent()
                ├─ isWindowOpen(), close()
                └─ getSpriteManager()

                SpriteManager (Управление ресурсами)
                ├─ textureList: vector<string>
                ├─ textures: unordered_map<uint64_t, sf::Texture*>
                ├─ loadTexture(string)
                ├─ getTexture(uint64_t)
                └─ hash(string)
```

### **7. Игровая логика**

```
                FightController (Контроллер боя)
                ├─ physModule: PhysicsModule&
                ├─ graphModule: GraphicsModule&
                ├─ currentFight: Fight
                ├─ currentStatus: Status (Stopped, Fighting, Paused, Final)
                ├─ currentRoundNum: uint8_t
                ├─ startFight(), restartFight(), stopFight()
                ├─ mousePressed(), mouseReleased(), mouseMoved()
                ├─ keyPressed(), keyReleased()
                └─ timeEvent(Time_t dt)

                Fight (Логика боя)
                ├─ lftPlayer, rgtPlayer: Fighter
                ├─ objects: vector<GameObject*>
                ├─ timer: Time_t
                ├─ sp_man: SpriteManager&
                ├─ status: Status
                ├─ restart(Time_t)
                ├─ mousePressed(), mouseReleased(), mouseMoved()
                ├─ keyPressed(), keyReleased()
                └─ timeEvent(Time_t dt)

                FighterInfo (Информация о бойце)
                ├─ textureID: uint64_t
                ├─ strength, dexterity, constitution: uint8_t
                ├─ KeyControls: map<Key, Commands>
                ├─ MouseControls: map<MouseButton, Commands>
                ├─ Commands: MOVE_LEFT, MOVE_RIGHT, JUMP, SIT, ATTACK_WITH_HAND, ATTACK_WITH_LEG
                └─ getCommand(), setKeyControls(), setMouseControls()
```

### **8. Математические и геометрические классы**

```
                Vec2 (Векторная математика)
                ├─ x, y: double
                ├─ GetLen(), Normalize(), Rotate(double)
                ├─ normalized(), dot(Vec2), length(), lengthSquared()
                ├─ Операторы: +, -, *, /, +=, -=, *=, /=
                ├─ Операторы сравнения: <, <=, >, >=, ==
                └─ GetAngle(), GetRandAngle()

                Rect (Геометрия)
                ├─ x, y, w, h: double
                ├─ getPos(), getSize(), getCenter()
                ├─ left(), right(), top(), bot()
                ├─ contains(Rect), contains(Vec2)
                ├─ move(Vec2), print()
                └─ checkRectCollision(Rect, Rect)

                Hitbox (Система коллизий)
                ├─ rects: vector<Rect>
                ├─ type: Type (Basic, Attack, Block)
                ├─ active: bool
                ├─ addRect(Rect)
                ├─ getRects()
                └─ checkHitboxCollision(Hitbox, Hitbox)
```

## 🔄 Поток данных и связей

### **Основной цикл обновления**

```
1. MAIN LOOP
   ├─ Обработка SFML событий
   ├─ Конвертация в внутренние события
   └─ Вызов EventManager

2. EventManager
   ├─ Распределение событий по обработчикам
   ├─ Вызов соответствующих методов объектов
   └─ Синхронизация времени

3. Обновление физики
   ├─ PhysicsModule::updateObjects(dt)
   ├─ Применение физики к Entity
   ├─ Проверка коллизий
   └─ Обновление SpatialGrid

4. Синхронизация графики
   ├─ GameObject::updateGraphicsPosition()
   ├─ Конвертация мировых координат в экранные
   ├─ Обновление SFML спрайтов
   └─ Применение трансформаций камеры

5. Отрисовка
   ├─ GraphicsModule::draw()
   ├─ Отрисовка всех Drawable объектов
   └─ Обновление экрана
```

### **Связи между модулями**

```
GraphicsModule ←→ SpriteManager (управление ресурсами)
GraphicsModule ←→ GameObject (отрисовка)
PhysicsModule ←→ Entity (физика)
PhysicsModule ←→ SpatialGrid (оптимизация коллизий)
FightController ←→ PhysicsModule (управление физикой)
FightController ←→ GraphicsModule (управление графикой)
GameObject ←→ Entity + DrawableSprite (мост между движками)
CoordinateSystem ←→ GameObject (конвертация координат)
EventManager ←→ Все модули (события)
```

## 📊 Статистика архитектуры

- **Всего классов**: 25+
- **Уровней наследования**: 3-4
- **Интерфейсов**: 5 (абстрактных классов)
- **Модулей**: 8 основных
- **Связей между модулями**: 15+
- **Паттерны**: Observer (события), Bridge (GameObject), Factory (создание объектов)

## 🎯 Принципы архитектуры

1. **Разделение ответственности** - каждый модуль отвечает за свою область
2. **Слабая связанность** - модули взаимодействуют через интерфейсы
3. **Высокая когезия** - связанные функции группируются в классах
4. **Расширяемость** - легко добавлять новые типы объектов и функциональность
5. **Производительность** - оптимизированные алгоритмы и структуры данных
6. **Тестируемость** - модульная структура упрощает тестирование

## 🚀 Преимущества архитектуры

✅ **Модульность** - независимые компоненты
✅ **Масштабируемость** - легко расширять
✅ **Производительность** - оптимизированные алгоритмы
✅ **Читаемость** - четкая структура и связи
✅ **Поддерживаемость** - простое внесение изменений
✅ **Переиспользование** - компоненты можно использовать в других проектах
