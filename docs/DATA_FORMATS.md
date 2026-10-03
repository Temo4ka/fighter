# Форматы данных боя

Что лежит в `data/`, кто это читает и что значит каждое поле. Зафиксировано в волне 0
фазы 2 (задача 2.0.3, docs/DEVELOPMENT_PLAN.md); меняется только через ревью.
Как подбирать значения на сеансах настройки — в [TUNING.md](TUNING.md).

## Общие правила

- Неизвестное поле — ошибка: опечатка в ключе не должна молча превращаться в значение
  по умолчанию. Сообщение об ошибке называет файл, поле и значение.
- Части тела пишутся как в `getBodyPartName()` (`Head`, `Torso`, `ForearmR`…), слоты —
  как в `getEquipmentSlotName()` (`Head`, `Weapon`…), ступени реакции — как в
  `getReactionLevelName()` (`None`, `Touch`, `Flinch`, `Stagger`, `Knockback`, `Knockdown`).
- Ключи новых файлов — `snake_case` (как в `items/` и `fighters/`). Старые файлы
  (`combat.json`, `rigs/`, `poses/`) пишутся в `camelCase`; их не переименовываем,
  чтобы не ломать загрузчики без нужды.
- Всё, что относится к бою, перечитывается при создании `Battle`, поэтому `F5` в
  песочнице подхватывает правки.

| Файл | Что | Кто читает | Состояние |
|---|---|---|---|
| `rigs/*.json` | тело: части, суставы, параметры управления | `rig::loadRigDef` | есть |
| `poses/*.json` | клипы поз | `anim::loadClip` | есть; новые клипы — задача 2.2 |
| `combat.json` | параметры боя вне тел (расстояния, порог попадания, усталость) | `combat::loadCombatTuning` | есть |
| `items/*.json` | снаряжение и свойства оружия | `stats::loadItemCatalog` | есть |
| `fighters/*.json` | листы бойцов | `stats::loadFighterSheet` | есть |
| `moves/*.json` | удары | `combat::loadMoveSet` | есть |
| `reactions.json` | сила удара → урон и ступень реакции, накопление, блок | `combat::loadReactionTable` | есть |
| `balance.json` | статы → физический профиль | `stats` | пример; загрузчик — задача 2.4 |
| `visuals.json` | картинки частей тела, предметов и эффекты | рендер | черновик; окончательно — задача 2.5 |

## `moves/*.json` — удары

Один файл — один удар; имя файла — его id (`moves/jab.json` → `jab`). Id попадает в
события (`StrikeStarted::MoveId`), в `FighterView::MoveId` и в статистику
`BattleResult`.

```json
{
    "button": "HeavyPunch",
    "clip": "sword_slash",
    "weapon": "sword",
    "damage": 1.4,
    "min_reaction": "Flinch",
    "stamina": 14,
    "min_startup_sec": 0.3
}
```

| Поле | Обязательно | Что значит |
|---|---|---|
| `button` | да | кнопка: `Jab`, `HeavyPunch`, `BodyKick`, `LowKick` (О.1) |
| `clip` | да | клип `poses/<clip>.json`: позы, активная фаза, ударные части |
| `weapon` | нет | класс оружия (`weapon.class` предмета); без поля — удар без оружия, доступный всем |
| `damage` | да | множитель урона удара (О.4), ≥ 0 |
| `min_reaction` | нет | самая слабая реакция на чистое (не заблокированное) попадание, сколь бы слабым оно ни было; `Knockdown` нельзя. По умолчанию `None` |
| `stamina` | да | расход выносливости при начале удара (О.13), ≥ 0 |
| `min_startup_sec` | да | нижняя граница времени до активной фазы, с: быстрее не сделают ни DEX, ни оружие (О.7) |
| `close_clip` | нет | клип вплотную: играется вместо `clip`, если в момент начала удара тазы бойцов ближе `close_range_m` (задача 2.3) |
| `close_range_m` | с `close_clip` | дистанция между тазами, м, ближе которой играется `close_clip`; ≥ 0 |
| `chain_to` | нет | кнопки, удары которых могут отменить восстановление этого удара после чистого попадания — короткие цепочки (О.7); окно и длина — `chainWindowSec`, `maxChainLength` в `combat.json` |

**Какой удар делает кнопка.** Если у бойца оружие класса `C` и есть удар с
`"weapon": "C"` на этой кнопке — он; иначе удар без оружия на этой кнопке
(`combat::findMove`). Два удара без оружия на одной кнопке, как и два удара одного
класса оружия, — ошибка загрузки.

**Скорость.** Клип задаёт номинальные тайминги. Бой ускоряет его в
`PhysicalProfile::AttackSpeedScale × weapon.speed_scale` раз, но так, чтобы активная
фаза начиналась не раньше `min_startup_sec`. Ориентир О.7: джеб 0,15–0,20 с, сильный
0,3–0,4 с, DEX сдвигает на ±25 %.

## `items/*.json` — свойства оружия

Предмет в слоте `Weapon` может нести блок `weapon` (О.12); остальные поля предмета —
как раньше (`stats/loading.hpp`).

```json
{
    "id": "short_sword", "name": "Short sword", "slot": "Weapon",
    "covers": ["ForearmR"], "mass_kg": 1.2, "armor": 0.0,
    "weapon": { "class": "sword", "reach_m": 0.55, "speed_scale": 1.0, "power_scale": 1.15 }
}
```

| Поле | Что значит | Пределы |
|---|---|---|
| `class` | класс оружия: выбирает его удары в `moves/` | непустой |
| `reach_m` | насколько оружие выступает за кулак, м; тело (2.1) удлиняет им форму предплечья | 0…1,5 |
| `speed_scale` | множитель скорости ударов (тяжёлое оружие < 1) | 0,25…4 |
| `power_scale` | множитель урона ударов | 0,25…4 |

Масса оружия — по-прежнему `mass_kg`: она ложится на `covers` и уже меняет физику удара.
Блок `weapon` вне слота `Weapon` — ошибка.

## `reactions.json` — что делает попадание

Правила задачи 2.3 (бой), решения О.2, О.4 и ступени реакции.

```json
{
    "location": { "Head": 1.5, "Torso": 1.0, "Pelvis": 0.9, "...": 0.6 },
    "damage_per_strength": 4.0,
    "levels": [
        { "level": "Touch",     "min_strength": 0.3, "stun_sec": 0.0 },
        { "level": "Flinch",    "min_strength": 1.0, "stun_sec": 0.15 },
        { "level": "Stagger",   "min_strength": 2.0, "stun_sec": 0.35 },
        { "level": "Knockback", "min_strength": 3.5, "stun_sec": 0.5 },
        { "level": "Knockdown", "min_strength": 5.5, "stun_sec": 0.0 }
    ],
    "buildup": { "per_strength": 1.0, "decay_per_sec": 1.5, "threshold_drop": 0.08 },
    "block": { "damage_scale": 0.2, "max_level": "Touch", "stamina_per_strength": 3.0 }
}
```

**Сила удара**, м/с — из чего считаются урон и реакция:

    strength = impulse / victim_mass × location[part] × (1 − armor[part])

`impulse` — из `HitEvent`, `victim_mass` — масса всего тела жертвы, `armor` — броня
части, в которую попали (`PhysicalProfile::Parts`).

**Урон:** `damage = strength × damage_per_strength × move.damage × weapon.power_scale`
(`power_scale` — только для ударов оружием).

**Ступень:** самая сильная из `levels`, у которой

    strength ≥ min_strength × victim.Poise × max(1 − buildup × threshold_drop, 0.2)

но не слабее `move.min_reaction`. `Poise` — стойкость из профиля (CON, броня), `buildup` —
накопленное: каждое попадание добавляет `strength × per_strength`, со временем убывает
на `decay_per_sec` в секунду. Так серия слабых ударов может привести к пошатыванию, а
нокдаун остаётся редким (О.7). `stun_sec` — сколько боец не может действовать; для
нокдауна время задаёт тело (`rigs/*.json`: `knockdownSec`, `getUpSec`).

| Поле | Что значит |
|---|---|
| `location` | множитель места попадания для каждой части тела (все 13 обязательны) |
| `damage_per_strength` | HP за 1 м/с силы удара |
| `levels` | ступени по возрастанию `min_strength`; каждая из `Touch`…`Knockdown` ровно один раз |
| `buildup.per_strength` | сколько накопления даёт 1 м/с силы |
| `buildup.decay_per_sec` | на сколько накопление убывает за секунду |
| `buildup.threshold_drop` | на какую долю единица накопления снижает пороги |
| `block.damage_scale` | множитель урона при попадании в закрытую зону (О.2) |
| `block.max_level` | самая сильная реакция при блоке (О.2: «касание») |
| `block.stamina_per_strength` | расход выносливости блокирующего за 1 м/с силы |

## `balance.json` — статы в физику

Поля один к одному повторяют `stats::BalanceTable` (src/stats/stats.hpp); формулы —
`stats::computeProfile`. Задача 2.4 подключает файл и заменяет временные линейные
формулы. Пока файл совпадает с `BalanceTable::getDefaults()`.

| Поле | Что значит |
|---|---|
| `base_mass_kg` | масса частей тела при CON = 10 (все 13) |
| `mass_per_con` | +доля массы за очко CON выше 10 |
| `base_motor_torque`, `torque_per_str` | сила моторов корпуса и её рост от STR |
| `base_motor_gain`, `gain_per_dex` | как быстро моторы выходят на позу, рост от DEX |
| `move_speed_per_dex` | +доля скорости ходьбы за очко DEX |
| `attack_speed_per_dex` | +доля скорости ударов за очко DEX (±25 % на DEX 0/20, О.7) |
| `base_hp`, `hp_per_con` | HP и его рост от CON |
| `base_poise`, `poise_per_con` | стойкость (множитель порогов реакции) и её рост от CON |
| `base_stamina`, `stamina_per_con` | запас выносливости (О.13) |
| `base_stamina_regen`, `stamina_regen_per_con` | восстановление выносливости в секунду при CON = 10 и его рост |

Замедление при нулевой выносливости — правило боя, а не стата: `exhaustedSpeedScale`
в `combat.json`.

## `visuals.json` — картинки (черновик для 2.5)

Рендер рисует бойцов спрайтами частей тела через список отрисовки. Формат —
отправная точка; агент C уточняет его в задаче 2.5 через ревью.

```json
{
    "pixels_per_meter": 64,
    "skins": {
        "placeholder": {
            "texture": "assets/placeholder/humanoid.png",
            "parts": { "Head": { "rect": [0, 0, 24, 28], "origin": [12, 14] } }
        }
    },
    "items": {
        "iron_helmet": {
            "texture": "assets/placeholder/items.png",
            "parts": { "Head": { "rect": [0, 0, 26, 18], "origin": [13, 14] } }
        }
    },
    "effects": {
        "hit_flash": { "min_reaction": "Touch", "duration_sec": 0.06 },
        "camera_shake": { "min_reaction": "Knockback", "amplitude_m": 0.04, "duration_sec": 0.15 },
        "dust": { "on_knockdown": true, "particles": 8, "duration_sec": 0.5 }
    }
}
```

| Поле | Что значит |
|---|---|
| `pixels_per_meter` | масштаб: сколько пикселей текстуры на метр мира (для пиксельного режима — целое) |
| `skins.<id>.parts.<часть>` | прямоугольник `rect` = [x, y, ширина, высота] в текстуре, px; `origin` — точка, которая совпадает с центром части тела, px |
| `items.<id>.parts.<часть>` | картинка предмета поверх части тела; рисуется после тела |
| `effects` | неброские эффекты (О.7, п. 5): вспышка попадания, тряска камеры от сильного удара, пыль при падении; пороги — по ступени реакции из `StrikeLanded` |

Какой скин у бойца, решает приложение (не модуль боя): бой о картинках не знает.
