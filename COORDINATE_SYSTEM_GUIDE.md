# 🎯 Руководство по системе координат Fighter

## 📋 Обзор

Новая система координат в проекте Fighter решает проблемы разделения физических и графических координат, предоставляя:

- **Автоматическую конвертацию** между мировыми и экранными координатами
- **Систему камеры** с зумом, поворотом и следованием за целью
- **Масштабирование мира** для различных уровней детализации
- **Оптимизированную производительность** с матрицами трансформации

## 🏗️ Архитектура системы координат

### **Координатные пространства**

```cpp
enum Space {
    WORLD,      // Мировые координаты (физика) - Vec2(double)
    SCREEN,     // Экранные координаты (графика) - sf::Vector2f(float)
    UI          // Координаты пользовательского интерфейса
};
```

### **Основные компоненты**

```
┌─────────────────────────────────────────────────────────────┐
│                 CoordinateSystem                           │
├─────────────────────────────────────────────────────────────┤
│  Camera  │  WorldSettings  │  Transform Matrices          │
│ (Камера) │  (Настройки)    │  (Матрицы трансформации)     │
├─────────────────────────────────────────────────────────────┤
│  worldToScreen()  │  screenToWorld()  │  worldToUI()      │
│  (Мир→Экран)     │  (Экран→Мир)      │  (Мир→UI)         │
└─────────────────────────────────────────────────────────────┘
```

## 🔧 Основные методы

### **Конвертация координат**

```cpp
// Мировые координаты → Экранные координаты
sf::Vector2f worldToScreen(const Vec2& worldPos) const;

// Экранные координаты → Мировые координаты  
Vec2 screenToWorld(const sf::Vector2f& screenPos) const;

// Мировые координаты → UI координаты
sf::Vector2f worldToUI(const Vec2& worldPos) const;

// UI координаты → Мировые координаты
Vec2 uiToWorld(const sf::Vector2f& uiPos) const;
```

### **Управление камерой**

```cpp
void setCameraPosition(const Vec2& pos);     // Установить позицию
void setCameraZoom(double zoom);             // Установить зум (0.1x - 10x)
void setCameraRotation(double rotation);     // Установить поворот
void followTarget(const Vec2& target, double smoothness = 0.1); // Следовать за целью
```

### **Настройки мира**

```cpp
void setWorldScale(double scale);            // Пикселей на метр
void setWorldBounds(const Vec2& bounds);     // Границы мира
void setGravity(const Vec2& gravity);        // Гравитация
```

## 📊 Типы координат

### **1. Мировые координаты (World)**
- **Тип**: `Vec2(double)` - высокая точность для физики
- **Единицы**: Метры или игровые единицы
- **Использование**: Физические вычисления, коллизии, логика игры

```cpp
Vec2 playerPos(5.0, 3.0);        // 5 метров вправо, 3 метра вниз
Vec2 gravity(0.0, -9.8);         // Гравитация в м/с²
Vec2 velocity(10.0, 0.0);        // Скорость в м/с
```

### **2. Экранные координаты (Screen)**
- **Тип**: `sf::Vector2f(float)` - оптимизировано для SFML
- **Единицы**: Пиксели экрана
- **Использование**: Отрисовка, позиционирование спрайтов

```cpp
sf::Vector2f screenPos = toScreen(worldPos);  // Автоматическая конвертация
sprite.setPosition(screenPos);                 // Установка позиции спрайта
```

### **3. UI координаты**
- **Тип**: `sf::Vector2f(float)` - для интерфейса
- **Единицы**: Пиксели экрана (без трансформации камеры)
- **Использование**: Пользовательский интерфейс, HUD

```cpp
sf::Vector2f uiPos = toUI(worldPos);         // Конвертация для UI
text.setPosition(uiPos);                      // Позиция текста
```

## 🎮 Практические примеры

### **Пример 1: Создание игрового объекта**

```cpp
// Создаем объект в мировых координатах
Vec2 worldPosition(10.0, 5.0);    // 10 метров вправо, 5 метров вниз
Vec2 worldScale(2.0, 2.0);       // 2x2 метра

GameObject* player = new GameObject(
    texture,           // Текстура
    worldPosition,     // Мировая позиция
    worldScale,        // Мировой масштаб
    Entity::ON_MOVE,   // Состояние
    Vec2(0, 0),       // Скорость
    80.0               // Масса
);

// Графика автоматически синхронизируется!
// Не нужно вызывать applyPhysToGraph()
```

### **Пример 2: Управление камерой**

```cpp
// Следуем за игроком
Vec2 playerPos = player->getPosition();
g_coordSystem.followTarget(playerPos, 0.1);  // Плавное следование

// Устанавливаем зум для детального просмотра
g_coordSystem.setCameraZoom(2.0);            // 2x увеличение

// Поворачиваем камеру для эффектов
g_coordSystem.setCameraRotation(0.1);        // Небольшой поворот
```

### **Пример 3: Проверка видимости**

```cpp
// Проверяем, виден ли объект на экране
if (g_coordSystem.isInScreen(object->getPosition())) {
    // Объект виден - отрисовываем
    graphics.drawObject(object);
} else {
    // Объект не виден - пропускаем отрисовку
    // Экономия производительности!
}
```

### **Пример 4: UI элементы**

```cpp
// Создаем UI элемент в мировых координатах
Vec2 uiWorldPos(0, 0);  // Левый верхний угол мира
sf::Vector2f uiPos = toUI(uiWorldPos);

// Позиционируем UI элемент
healthBar.setPosition(uiPos);
scoreText.setPosition(uiPos + sf::Vector2f(10, 10));
```

## ⚡ Оптимизации

### **1. Матрицы трансформации**
```cpp
// Предвычисленные матрицы для быстрой конвертации
sf::Transform worldToScreenTransform;
sf::Transform screenToWorldTransform;

// Обновляются только при изменении камеры
void updateTransforms();
```

### **2. Пространственное разделение**
```cpp
// Проверка видимости перед отрисовкой
bool isInScreen(const Vec2& worldPos) const;
bool isInWorld(const Vec2& worldPos) const;
```

### **3. Кэширование результатов**
```cpp
// Глобальные утилитарные функции для быстрого доступа
inline sf::Vector2f toScreen(const Vec2& worldPos);
inline Vec2 toWorld(const sf::Vector2f& screenPos);
```

## 🔄 Автоматическая синхронизация

### **GameObject - мост между движками**

```cpp
class GameObject : public DrawableSprite, public Entity {
public:
    // Автоматическая синхронизация при изменении физических свойств
    void setPosition(const Vec2& newPos) {
        Entity::position = newPos;
        updateGraphicsPosition();  // Автоматически!
    }
    
    void setScale(const Vec2& newScale) {
        Entity::scale = newScale;
        updateGraphicsPosition();  // Автоматически!
    }
    
    void setRotation(double newRotation) {
        Entity::rotation = newRotation;
        updateGraphicsPosition();  // Автоматически!
    }
};
```

### **Преимущества автоматической синхронизации**

1. **Никаких забытых вызовов** `applyPhysToGraph()`
2. **Консистентность данных** между физикой и графикой
3. **Упрощение кода** - меньше ручной работы
4. **Производительность** - только при необходимости

## 📈 Настройки производительности

### **Рекомендуемые параметры**

```cpp
// Для файтинг-игры
g_coordSystem.setWorldScale(100.0);      // 100 пикселей = 1 метр
g_coordSystem.setWorldBounds(Vec2(2000, 1500));  // Мир 20x15 метров

// Для платформера
g_coordSystem.setWorldScale(50.0);       // 50 пикселей = 1 метр
g_coordSystem.setWorldBounds(Vec2(5000, 3000));  // Мир 100x60 метров

// Для стратегии
g_coordSystem.setWorldScale(10.0);       // 10 пикселей = 1 метр
g_coordSystem.setWorldBounds(Vec2(10000, 8000)); // Мир 1000x800 метров
```

### **Настройки камеры**

```cpp
// Плавное следование
g_coordSystem.followTarget(target, 0.05);  // Медленно

// Быстрое следование  
g_coordSystem.followTarget(target, 0.3);   // Быстро

// Ограничения зума
g_coordSystem.setCameraZoom(0.5);  // Минимум 0.5x
g_coordSystem.setCameraZoom(3.0);  // Максимум 3.0x
```

## 🚀 Расширенные возможности

### **1. Заворачивание мира**
```cpp
// Объекты появляются с противоположной стороны
g_coordSystem.setWorldWrap(true);

// Полезно для:
// - Платформеров с бесконечным миром
// - Игр типа Asteroids
// - Параллакс-эффектов
```

### **2. Множественные камеры**
```cpp
// Можно создать несколько экземпляров для разных областей
CoordinateSystem minimapCamera;
CoordinateSystem mainCamera;
CoordinateSystem uiCamera;

// Каждая со своими настройками
minimapCamera.setCameraZoom(0.1);  // Миникарта
mainCamera.setCameraZoom(1.0);     // Основная камера
uiCamera.setCameraZoom(1.0);       // UI без трансформации
```

### **3. Параллакс-эффекты**
```cpp
// Разные слои с разной скоростью камеры
Vec2 backgroundOffset = camera.position * 0.5;  // Медленнее
Vec2 midgroundOffset = camera.position * 0.8;   // Средне
Vec2 foregroundOffset = camera.position * 1.0;  // Обычно
```

## 🧪 Тестирование

### **Запуск тестов**
```bash
# Компиляция теста
clang++ -std=c++23 test_coordinates.cpp source/coordinate_system.cpp \
         source/vec2.cpp source/physics.cpp source/hitbox.cpp \
         source/geometry.cpp -I/opt/homebrew/Cellar/sfml/3.0.0/include \
         -L/opt/homebrew/Cellar/sfml/3.0.0/lib -lsfml-graphics \
         -lsfml-window -lsfml-system -o test_coordinates

# Запуск
./test_coordinates
```

### **Что тестируется**
- ✅ Конвертация координат (точность)
- ✅ Камера (позиция, зум, поворот)
- ✅ Масштабирование мира
- ✅ Границы мира
- ✅ Следование за целью
- ✅ UI координаты
- ✅ Производительность

## 📝 Миграция с старой системы

### **Быстрая замена**

```cpp
// Старый код
void applyPhysToGraph() {
    setPosition(sf::Vector2f(position.x, position.y));
}

// Новый код - автоматически!
// Ничего не нужно делать - GameObject сам синхронизирует
```

### **Обновление существующих объектов**

```cpp
// Старый способ создания
Entity* obj = new Entity(state, pos, vel, mass);

// Новый способ
GameObject* obj = new GameObject(texture, pos, scale, state, vel, mass);
// Автоматическая синхронизация включена!
```

## 🎯 Заключение

Новая система координат предоставляет:

- **🎯 Точность**: Разделение физических и графических координат
- **⚡ Производительность**: Оптимизированные матрицы трансформации
- **🔄 Автоматизация**: Синхронизация без ручного вмешательства
- **📷 Гибкость**: Камера с зумом, поворотом и следованием
- **🌍 Масштабируемость**: Настраиваемые размеры мира
- **🛠️ Простота**: Простые утилитарные функции

Система готова для использования в продакшене и может быть легко расширена для будущих потребностей игры.
