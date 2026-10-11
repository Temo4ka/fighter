# Архитектура

Как устроен код сейчас — для того, кто его меняет: модули, соглашения, шаг боя, отладочный слой, стиль.
Как работает сам бой — [MECHANICS.md](MECHANICS.md); куда движется проект — [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md).

Содержание:
1. [Модули и зависимости](#1-модули-и-зависимости)
2. [Соглашения](#2-соглашения)
3. [Шаг боя](#3-шаг-боя)
4. [События](#4-события)
5. [Отладочный слой](#5-отладочный-слой)
6. [Стиль кода](#6-стиль-кода)
7. [Как добавить модуль](#7-как-добавить-модуль)
8. [Сторонние библиотеки](#8-сторонние-библиотеки)

## 1. Модули и зависимости

Каждый каталог `src/<модуль>/` — статическая библиотека `fighter::<модуль>`; зависимости идут строго сверху вниз.

Зависимости задаются в `src/<модуль>/CMakeLists.txt` через `fighter_add_module`. Модуль, не указанный
в зависимостях, подключить не получится — сборка упадёт. Обратных зависимостей нет.

| Модуль | Что внутри | Зависит от (публично; приватно) |
|---|---|---|
| `core` | `Vec2`, `log`, `Signal`/`Connection`, `FixedStepLoop`, `BodyPart`, `PartTransform`, `readTextFile` | — |
| `debug` | `debug::draw*` (API отрисовки), `DrawList`, категории, палитра | `core` |
| `physics` | `World` (тела, формы, шарниры, шаг, попадания), `Body` (динамические и кинематические), `RevoluteJoint`, `SpringJoint` (хват двуручного), `HitEvent`; Box2D debug draw → `debug::` | `core`, `debug`; Box2D |
| `rig` | `RigDef` (риг из JSON), `Rig` — гибридное тело: `PelvisController`, позы из клипов, стопы, моторы, жёсткость, отбрасывание, нокдаун, разворот, предметы в руках (`HeldItem`); `ContactResolver` (`rig/contact.hpp`) и расталкивание (`rig/spacing.hpp`) | `core`, `debug`, `physics`; json |
| `anim` | `Pose`, `Clip`, `sampleClip`, `layerPose`, слои ног и верха тела, зеркальные ноги и переставленные руки (`anim/layers.hpp`) | `core`; json |
| `stats` | `Stats`, `Loadout`, `PhysicalProfile`, `computeProfile`, `ItemCatalog`, `FighterSheet`, загрузка JSON | `core`; json |
| `combat` | публичный API: `Battle`, `BattleConfig`, `BattleEvent`, `BattleResult`, `PlayerCommands`, `RenderSnapshot`; внутри — `Fighter`, `CombatTuning`, `MoveDef`, наборы движений, `checkData`, `measureMove` и `MoveRun` (стенд; `BattleConfig::ClipOverrides` — тексты клипов вместо файлов), `LegCycle`, `LegStep` | `core`, `stats`, `physics` (только `HitEvent`); `debug`, `rig`, `anim`, json |
| `editor` | логика редактора поз без окна: прямая кинематика (`poseBody`), призрак (`composeGhostPose`, `findStance`), правка ключей (`clip_edit.hpp`), IK (`arm_ik.hpp`, `pose_drag.hpp`), запись клипа (`clip_writer.hpp`), факт (`physics_preview.hpp`), списки Open (`catalog.hpp`), траектории, метрики, предупреждения (`clip_check.hpp`), пороги (`editor_config.hpp`), история (`clip_history.hpp`), копирование позы (`pose_ops.hpp`), новый клип (`new_clip.hpp`) | `core`, `anim`, `rig`, `stats`, `combat`; `debug`, json |
| `render` | `Camera`, `Resources`, `Visuals`, `RenderList` и `drawRenderList`, `buildRenderList`, `BattleEffects`, `PixelLayout`, `BattleRenderer`, `DebugOverlay` | `core`, `debug`, `combat` (снимок и события), SFML; json |
| `ui` | меню и экраны: машина экранов (без окна) и их отрисовка, `ui.json` | `core`, `combat`, `render`, `stats`, SFML; json |
| `app` | `App`, `InputSystem`, `main`, проверка данных, демо-сценарии; окно редактора поз (`app/pose_editor.cpp`, только debug) | `core`, `debug`, `combat`, `editor`, `render`, `ui`, SFML; ImGui-SFML (debug) |

`combat` и всё под ним не зависят от SFML: модуль боя встраивается во внешний проект без нашего рендера.
`ui` и `app` собираются только с `FIGHTER_BUILD_APP`. `tools/placeholder_gen` — утилита плейсхолдеров ([ART.md](ART.md)).

## 2. Соглашения

Правила, которые держит весь код.

**Координаты и единицы.** Мир — в метрах, килограммах, секундах; ось Y вверх, `(0, 0)` — центр арены на уровне пола.
- В коде углы — в радианах; в JSON-данных клипов и пределов — в градусах.
- Пиксели существуют только в `render::Camera`. В мире точку переводят через `Camera::toDraw()` и рисуют в
  `Cam.getWorldView()`; текст и HUD — в `Cam.getScreenView()`, точка — через `Cam.worldToPixel()`.
- Типы Box2D (`b2Vec2`, `b2BodyId`…) не выходят за пределы `src/physics/`.

**Цикл.** `FixedStepLoop` вызывает шаг симуляции с постоянным `dt = 1/60 с` столько раз, сколько накопилось времени.

```
loop:
  события ОС → InputSystem (команды на шаг для каждого игрока)
  accumulator += frameTime
  while accumulator >= dt:  battle.update(commands, dt); accumulator -= dt
  alpha = accumulator / dt
  render(combat::interpolate(Prev, Curr, alpha))
```

Порядок кадра — в `App::run()`: события → шаги симуляции → отрисовка. Порядок этапов явный, в коде,
и не зависит от порядка подписки. Симуляция детерминирована при одинаковом вводе — задел для ИИ, реплеев и сети.

**Ввод.** `InputSystem` хранит состояние клавиш по событиям нажатия и отпускания и на каждом шаге отдаёт
`PlayerCommands` каждому игроку. Боевой код не знает про клавиатуру: так же команды придут от ИИ или реплея.

**Владение.** У объекта один владелец (`std::unique_ptr` или член класса); бойцы и арена принадлежат `Battle`.
- Модули получают невладеющие ссылки; `shared_ptr` — только при доказанной общей собственности.
- Подписка на сигнал — `Connection`, она отписывается в деструкторе.
- `physics::World` владеет миром Box2D: копировать нельзя, перемещать можно. Поэтому и `Battle` только перемещается
  (реализация — за `std::unique_ptr<Simulation>`, заголовок не тянет physics, rig, anim).
- Композиция вместо наследования; `virtual` — только для взаимозаменяемых реализаций (будущий `Controller`).

**Рендер не видит физику.** `Battle` после каждого шага публикует `RenderSnapshot` (позиции частей, HP, таймер,
состояние); рендер читает только снимки и события.

**Список отрисовки.** Кадр строится в два шага.
1. `buildRenderList()` (`src/render/scene.hpp`) превращает снимок в общие примитивы — спрайт, капсула, прямоугольник,
   круг, полоска, текст — со слоем (фон → арена → дальний боец → ближний → эффекты → интерфейс).
2. `drawRenderList()` рисует любой список одним циклом и об игре ничего не знает.

- Какая картинка у части или предмета — в `data/visuals.json`, не в коде; часть без картинки рисуется капсулой.
- Новый тип примитива добавляется в `RenderList` и цикл один раз — редкий случай.
- Сборка списка не требует окна и проверяется тестами.
- Рисовать можно в любой `sf::RenderTarget`. В пиксельном стиле `BattleRenderer` рисует мир в `sf::RenderTexture`
  низкого разрешения камерой, привязанной к пиксельной сетке, и увеличивает без сглаживания (`render/pixel_view.hpp`);
  HUD и отладка — поверх, камерой `getOverlayCamera()`.

**Данные и live-тюнинг.** Риги, клипы и параметры боя лежат в `data/` и читаются при создании `Battle`
из `BattleConfig::DataDir`.
- Новый `Battle` — перечитанные файлы: `F5` и `Backspace` создают бой заново.
- Конструктор бросает `std::runtime_error`, если файл отсутствует или испорчен; песочница тогда оставляет старый
  бой и пишет ошибку в журнал. Неизвестный ключ — ошибка.
- Параметры — [TUNING.md](TUNING.md), форматы — [DATA_FORMATS.md](DATA_FORMATS.md).

## 3. Шаг боя

`Battle::update()` проходит этапы в фиксированном порядке; пользовательских колбэков внутри шага Box2D нет.

1. **План.** Бойцы планируют движение таза (`PelvisController`: ходьба, отброс, дорожка `pelvisX`), продвигают цикл
   ходьбы на запланированный путь и выбирают клипы (поза «после пути» и «без пути», `Rig::setTravelPose`).
2. **Контакт до шага** — `rig::ContactResolver::beforeStep`: стены, лежащий, расталкивание тел; ноги и цикл
   ходьбы идут на ту долю пути, которую таз прошёл.
3. **Управление** — `applyControl`: кинематические таз и ноги — к позе из клипов, перенос корпуса, уступающие руки,
   моторы физических частей (и запястья, `Rig::driveMotors`).
4. **Физика** — `World::step()`: несколько шагов Box2D (`physicsSteps`).
5. **Контакт после шага** — `ContactResolver::afterStep`: `Rig::stopPosedLimbs` обоих; ударная нога — до касания
   и `Fighter::onPosedStop`.
6. **Попадания** — разбор `HitEvent`: контакт ударной части в активной фазе, одно на атаку; импульс —
   `World::addHit`, ударные массы — `Rig::setStrikeMasses`.
7. **Последствия** — отбрасывание, выталкивание (`ContactResolver::onStrikeLanded`), просадка жёсткости или нокдаун.
8. **Выход** — снимок для рендера, события шага, отладочные примитивы.

`Battle::settle` (бой окончен) зовёт те же стадии контакта. Что делает каждая стадия и почему они отдельные —
[MECHANICS.md](MECHANICS.md), раздел 5.

Где что живёт:

| Механика | Код | Описание |
|---|---|---|
| слои позы: ноги и верх тела | `anim/layers.hpp`; `LegCycle`, `Fighter::placeStepFeet`; `LegStep`, `Rig::reachFoot`; `ClipLibrary::getMirrored` | [MECHANICS.md](MECHANICS.md), раздел 2 |
| выпад `pelvisX` | `PelvisController::setClipTravel`; `Rig::setPelvisDropLimit`; `Fighter::planLungeStep`, `Fighter::checkPelvisTrack` | раздел 3.5 |
| гибридное тело, перенос корпуса, нокдаун | `Rig` (`carryPhysicalParts`, `driveMotors`) | раздел 1 |
| остановка ног о соперника | `findPosedStop`, `rewindBody`, `Rig::stopPosedLimbs` | раздел 5.4 |
| проверка перекрытий | `Battle::findWorstOverlap` (обёртка стадии) | раздел 5.2 |
| оружие на запястье | тело с `physics::PartRef` предплечья; `anim::Pose::WeaponAngle`; `Rig::setWristAngle`; снимок `FighterView::Weapons` | раздел 6 |
| наборы движений и руки | `MoveLibrary::findEntry`, `PressWindow`, `MoveLibrary::canGrowCombo`; блок — `MoveLibrary::getBlock`, `isBlockedBy`; другая рука — `ClipLibrary::getOtherHand` | разделы 4.5, 6.1 |

## 4. События

Внутри симуляции — только данные, наружу — список событий шага.

- *Опросом* идёт то, что нужно каждый шаг или кадр: ввод (`PlayerCommands`), состояние для рендера (`RenderSnapshot`).
- *Событиями* — то, на что независимо реагируют несколько систем: попадание, блок, нокаут, конец раунда, кнопки меню.

```
Battle::update()  ──►  события шага: [StrikeLanded{...}, BattleOver{...}]
                                │
App после шага:                 ▼
   for (const auto& Event : Battle.getEvents())  ──►  Signal::emit  ──►  звук, эффекты, HUD, меню
```

- `Battle` не вызывает чужой код во время шага и не хранит подписчиков: копит события шага, `getEvents()` отдаёт
  их до следующего `update()`.
- `App` после шага рассылает события через `Signal` (`src/core/signal.hpp`) вместе со снимком (`App::BattleEvents`).
  Сейчас подписаны рендер (вспышки, тряска, пыль) и экраны меню (переход к итогам).
- Почему так: подписчики не влияют на порядок симуляции — детерминизм сохраняется. При сетевом откате `App` решает,
  повторять ли звуки и эффекты, а бой об этом не знает.
- Внутри симуляции модули обмениваются данными напрямую: физика после шага отдаёт `HitEvent`, бой обрабатывает
  их в фиксированном порядке.
- События — `std::variant` из `src/combat/events.hpp`: удар начат (`StrikeStarted`), удар попал (`StrikeLanded`:
  контакт, сила, урон, ступень, блок), нокдаун, вставание, выносливость кончилась, бой окончен.
  Что боец делает сейчас (стойка, атака и фаза, блок и зона, реакция, прижат к стене) — состояние в `FighterView`.

## 5. Отладочный слой

Любой модуль может нарисовать примитив в мировых координатах; в release все вызовы исчезают.

```cpp
#include "debug/draw.hpp"

debug::drawArrow(debug::Cat::Forces, Point, Impulse * 0.05f, "J=34");
debug::setPanel("P1 state", "attack");
debug::logEvent("P1 hit P2: 12 dmg");
```

- Примитивы — линии, стрелки, окружности, дуги, многоугольники, точки, кресты, текст — попадают в общий приёмник
  `debug::DrawList`; каждый относится к категории (`debug::Cat`). Рендер рисует список поверх сцены или вместо неё.
- Примитивы очищаются в начале каждого шага симуляции (`debug::beginTick()` в `App::stepSimulation`), поэтому
  на паузе картинка «замирает». Строки панели живут, пока их не перезапишут по тому же ключу.
- `debug::ScopedSide` задаёт, к какому бойцу относятся примитивы (цвет `Hurtbox`).
- В release (`FIGHTER_DEBUG=0`) `debug::draw*`, `setPanel`, `logEvent` — пустые inline, вызовы выбрасываются
  компилятором. Тяжёлую подготовку данных оборачивайте в `if constexpr (FIGHTER_DEBUG)`.
- Цвета категорий — только в `src/debug/palette.hpp`. Текст — шрифтом JetBrains Mono (OFL) из `assets/fonts/`.
- Глобальный приёмник `debug::getDrawList()` — единственное глобальное состояние в проекте, и только для отладки.

Категории, строки панели и клавиши — [TOOLS.md](TOOLS.md).

## 6. Стиль кода

Именование — по [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html#name-types-functions-variables-and-enumerators-properly).

| Что | Как | Пример |
|---|---|---|
| типы, перечисления, перечислители | `UpperCamelCase` | `FixedStepLoop`, `Cat::Hurtbox` |
| переменные, параметры, поля, константы | `UpperCamelCase`, без `k` и `_` | `MoveX`, `Dt`, `WalkSpeed` |
| функции и методы | глагол в `lowerCamelCase` | `computeProfile()`, `drawArrow()` |
| геттеры | `get…`, `is…`, `should…` | `getSnapshot()`, `isPaused()` |
| пространства имён | строчные | `fighter::combat` |
| файлы | `snake_case` | `fixed_step_loop.hpp` |

- Параметр не совпадает с именем поля класса: MSVC на `/W4` предупреждает (C4458). Используйте `NewX`, `Settings`, `RootPath`.
- Однобуквенных имён нет. Исключения — формулы и общепринятое: `X`/`Y` у `Vec2`, каналы `R`/`G`/`B`/`A`,
  операнды в `vec2.hpp`, коэффициент интерполяции `T`, параметр шаблона `T`.
- Циклы по контейнерам — range-based с `auto&`/`auto&&`; два контейнера — `std::views::zip`. Индексный цикл — только
  когда индекс нужен (переменная `Index`, а не `I`).
- `size_t`, `uint8_t`, `uint64_t` — без `std::`; подключайте `<cstddef>` и `<cstdint>`.
- Главные объявления — выше вспомогательных: публичная функция в начале `.cpp`, помощники в анонимном
  пространстве имён — после неё.
- Комментарии — на английском. Документирующие — `///`, обычные — `//`.
- Каждый `.hpp` начинается с заголовка LLVM: первая строка ровно 80 символов, затем `\file` и зачем файл нужен.

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

## 7. Как добавить модуль

Модуль — каталог, строка в `src/CMakeLists.txt` и тесты.

1. Каталог `src/<имя>/` с `CMakeLists.txt` (без `SOURCES` — библиотека только из заголовков):
   ```cmake
   fighter_add_module(<имя>
       SOURCES  a.cpp b.cpp
       PUBLIC_DEPS  fighter::core
   )
   ```
2. `add_subdirectory(<имя>)` в `src/CMakeLists.txt` — после модулей, от которых он зависит.
3. Тесты — в `tests/<имя>/`, файлы перечисляются в `tests/CMakeLists.txt`. Имена `TEST_CASE` — только латиницей:
   на Windows ctest передаёт их в Catch2 в ANSI-кодировке, и кириллица ломает фильтр.

## 8. Сторонние библиотеки

Все подключаются в `cmake/Dependencies.cmake` через `FetchContent` с проверкой SHA256; новая — только после согласования.

| Библиотека | Версия | Зачем |
|---|---|---|
| SFML | 3.1.0 | окно, ввод, 2D-графика (модули audio и network отключены) |
| Box2D | 3.1.1 | физика твёрдых тел и шарниров |
| nlohmann/json | 3.12.0 | данные в JSON |
| Catch2 | 3.16.0 | тесты |
| Dear ImGui | 1.91.9b | панели редактора поз (только debug-сборка) |
| ImGui-SFML | 3.0 | связка ImGui с SFML 3 (только debug; проверена с ImGui 1.91, поэтому не 1.92) |
