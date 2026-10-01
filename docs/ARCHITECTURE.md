# Архитектура

Как устроен код **сейчас**. Куда он движется — в [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md).

## Модули и зависимости

Каждый каталог `src/<модуль>/` — отдельная статическая библиотека `fighter::<модуль>`.
Зависимости задаются в `src/<модуль>/CMakeLists.txt` через `fighter_add_module`
и идут строго сверху вниз: модуль, который не указан в зависимостях, подключить
не получится — сборка упадёт.

```
app ──► render ──► combat ──► stats ──► core
 │         │          │
 │         │          ├──► physics ──► Box2D (PRIVATE)
 │         │          ├──► rig, anim
 │         └──► SFML  │
 └────────────────────┴──► debug ──► core
```

| Модуль | Что внутри | SFML | Box2D |
|---|---|---|---|
| `core` | `Vec2`, `log`, `Signal`/`Connection`, `FixedStepLoop`, `BodyPart`, `PartTransform` | — | — |
| `debug` | `debug::draw*` (API отрисовки), `DrawList`, категории, палитра | — | — |
| `physics` | `physics::World`, `HitEvent` | — | внутри |
| `rig`, `anim` | пока только заголовки | — | — |
| `stats` | `Stats`, `Loadout`, `PhysicalProfile`, `computeProfile` | — | — |
| `combat` | `Battle`, `BattleConfig`, `BattleResult`, `PlayerCommands`, `RenderSnapshot` | — | — |
| `render` | `Camera`, `Resources`, `BattleRenderer`, `DebugOverlay` | да | — |
| `app` | `App`, `InputSystem`, `main` | да | — |

`combat` и всё под ним не зависят от SFML: модуль боя встраивается во внешний проект
без нашего рендера.

## Соглашения

**Координаты.** Мир в метрах, ось Y вверх, `(0, 0)` — центр арены на уровне пола.
Пиксели существуют только в `render::Camera`. Рисуя в мире, переводите точку через
`Camera::toDraw()` и рисуйте в `Cam.getWorldView()`; текст и HUD рисуются в
`Cam.getScreenView()`, точка переводится через `Cam.worldToPixel()`.

**Цикл.** `FixedStepLoop` вызывает шаг симуляции с постоянным `dt = 1/60 с` столько раз,
сколько накопилось реального времени. Рендер получает `alpha` и рисует
`combat::interpolate(Prev, Curr, Alpha)`. Порядок кадра — в `App::run()`:
события → шаги симуляции → отрисовка.

**Ввод.** `InputSystem` хранит состояние клавиш по событиям нажатия и отпускания
и на каждом шаге отдаёт `PlayerCommands` для каждого игрока. Боевой код
не знает про клавиатуру: так же команды будут приходить от ИИ или реплея.

**Владение.** У объекта один владелец (`std::unique_ptr` или член класса). Модули
получают ссылки. Подписка на сигнал — `Connection`, она отписывается в деструкторе.
`physics::World` владеет миром Box2D, копировать его нельзя, перемещать можно.

**Рендер не видит физику.** `Battle` после каждого шага публикует `RenderSnapshot`
(позиции, HP, таймер). Рендер читает только снимки.

## Отладочный слой

Любой модуль может нарисовать примитив в мировых координатах:

```cpp
#include "debug/draw.hpp"

debug::drawArrow(debug::Cat::Forces, Point, Impulse * 0.05f, "J=34");
debug::setPanel("P1 state", "attack");
debug::logEvent("P1 hit P2: 12 dmg");
```

- Примитивы очищаются в начале каждого шага симуляции (`debug::beginTick()`
  в `App::stepSimulation`), поэтому на паузе картинка «замирает».
- Строки панели живут, пока их не перезапишут по тому же ключу.
- `debug::ScopedSide` задаёт, к какому бойцу относятся примитивы (цвет Hurtbox).
- В release (`FIGHTER_DEBUG=0`) все функции `debug::draw*`, `setPanel`, `logEvent` — пустые inline, и вызовы
  выбрасываются компилятором. Тяжёлую подготовку данных для отладки оборачивайте
  в `if constexpr (FIGHTER_DEBUG)`.
- Цвета категорий — только в `src/debug/palette.hpp`.

Глобальный приёмник `debug::getDrawList()` — единственное глобальное состояние
в проекте, и только для отладки.

## Стиль кода

Именование — по [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html#name-types-functions-variables-and-enumerators-properly):

| Что | Как | Пример |
|---|---|---|
| типы, перечисления, перечислители | `UpperCamelCase` | `FixedStepLoop`, `Cat::Hurtbox` |
| переменные, параметры, поля, константы | `UpperCamelCase`, без `k` и `_` | `MoveX`, `Dt`, `WalkSpeed` |
| функции и методы | глагол в `lowerCamelCase` | `computeProfile()`, `drawArrow()` |
| геттеры | `get…`, `is…`, `should…` | `getSnapshot()`, `isPaused()` |
| пространства имён | строчные | `fighter::combat` |
| файлы | `snake_case` | `fixed_step_loop.hpp` |

- Параметр не должен совпадать с именем поля класса: MSVC на `/W4` предупреждает
  об этом (C4458). Используйте `NewX`, `Settings`, `RootPath` и т.п.
- Однобуквенных имён нет. Исключения — формулы и общепринятые обозначения:
  `X`/`Y` у `Vec2`, каналы `R`/`G`/`B`/`A`, операнды в `vec2.hpp`, коэффициент
  интерполяции `T`, параметр шаблона `T`.
- Циклы по контейнерам — range-based с `auto&`/`auto&&`; два контейнера
  параллельно — через `std::views::zip`. Индексный цикл — только когда индекс
  действительно нужен (тогда переменная называется `Index`, а не `I`).
- `size_t`, `uint8_t`, `uint64_t` и т.д. пишутся без `std::`; подключайте
  `<cstddef>` (для `size_t`) и `<cstdint>` (для `uintN_t`).
- Главные объявления — выше вспомогательных: публичная функция в начале
  `.cpp`, её помощники в анонимном пространстве имён — после неё.
- Комментарии — на английском. Документирующие — `///`, обычные — `//`.
- Каждый `.hpp` начинается с заголовка в формате LLVM: первая строка ровно
  80 символов, затем `\file` и описание, зачем файл нужен:

```cpp
//===- core/vec2.hpp - 2D vector math ---------------------------*- C++ -*-===//
//
// Part of the Fighter project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines Vec2, the 2D vector used for all world-space math, ...
///
//===----------------------------------------------------------------------===//
```

## Как добавить модуль

1. Каталог `src/<имя>/` с `CMakeLists.txt`:
   ```cmake
   fighter_add_module(<имя>
       SOURCES  a.cpp b.cpp
       PUBLIC_DEPS  fighter::core
   )
   ```
   Без `SOURCES` получится библиотека только из заголовков.
2. `add_subdirectory(<имя>)` в `src/CMakeLists.txt` — после модулей, от которых он зависит.
3. Тесты — в `tests/<имя>/`, файлы перечисляются в `tests/CMakeLists.txt`.
   Имена `TEST_CASE` пишутся только латиницей: на Windows ctest передаёт их
   в Catch2 через командную строку в ANSI-кодировке, и кириллица ломает фильтр.

## Сторонние библиотеки

Все подключаются в `cmake/Dependencies.cmake` через `FetchContent` с проверкой SHA256.

| Библиотека | Версия | Зачем |
|---|---|---|
| SFML | 3.1.0 | окно, ввод, 2D-графика (модули audio и network отключены) |
| Box2D | 3.1.1 | физика твёрдых тел и шарниров |
| nlohmann/json | 3.12.0 | конфиги (с фаз 1–2) |
| Catch2 | 3.16.0 | тесты |

Новая библиотека — только после согласования.
