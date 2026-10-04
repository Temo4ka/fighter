# Лицензии ассетов

Каждый файл в `assets/` — здесь: откуда он, кто автор, по какой лицензии. Файл без записи
не подключается (docs/ART.md). Подходят CC0 и CC-BY (автор указывается здесь); наборы с
запретом коммерческого использования не берём. Лицензию проверяем у каждого файла.

| Файл | Источник | Автор | Лицензия | Статус |
|---|---|---|---|---|
| `fonts/JetBrainsMono-Regular.ttf` | https://github.com/JetBrains/JetBrainsMono | The JetBrains Mono Project Authors | SIL OFL 1.1 (`fonts/OFL.txt`) | ✅ можно |
| `backgrounds/bg.jpeg` | неизвестно | неизвестно | неизвестно | ⚠️ используется как фон (`render/assets.hpp`); заменить в T.6 или до выпуска |
| `backgrounds/blueBG.jpg` | неизвестно | неизвестно | неизвестно | ⚠️ не используется; удалить или выяснить происхождение |
| `objects/wood_block.png` | неизвестно | неизвестно | неизвестно | ⚠️ не используется; удалить или выяснить происхождение |
| `players/player1.jpg` | неизвестно | неизвестно | неизвестно | ⚠️ не используется; удалить или выяснить происхождение |
| `players/player2.jpg` | неизвестно | неизвестно | неизвестно | ⚠️ не используется; удалить или выяснить происхождение |
| `placeholders/pixel/humanoid/*.png` (13 файлов) | сгенерировано `tools/placeholder_gen` по `data/rigs/humanoid.json` | собственная работа проекта | как у проекта | ✅ можно; плейсхолдер, заменяется в T.5/T.6 |
| `placeholders/smooth/humanoid/*.png` (13 файлов) | сгенерировано `tools/placeholder_gen` по `data/rigs/humanoid.json` | собственная работа проекта | как у проекта | ✅ можно; плейсхолдер, заменяется в T.5/T.6 |
| `placeholders/pixel/items/<id>/*.png` (`iron_helmet/Head.png`, `short_sword/ForearmR.png`) | сгенерировано `tools/placeholder_gen` по `data/visuals.json` и `data/items/` | собственная работа проекта | как у проекта | ✅ можно; плейсхолдер |
| `placeholders/smooth/items/<id>/*.png` (`iron_helmet/Head.png`, `short_sword/ForearmR.png`) | сгенерировано `tools/placeholder_gen` по `data/visuals.json` и `data/items/` | собственная работа проекта | как у проекта | ✅ можно; плейсхолдер |

Файлы с ⚠️ пришли из старой версии проекта. Пока проект учебный и не распространяется,
они не мешают; перед любой публикацией — заменить или удалить (трек «Готовность к продукту», П.1).
