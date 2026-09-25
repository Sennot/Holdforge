# Проверенные источники и формат

Обновление 24 сентября, версия 0.2.0-beta.1: добавлены `Trajectory`/`Workflow`.
По Silicate `hooks/GJBaseGameLayer.cpp` воспроизведение подаёт события через
`processQueuedButtons`; `hooks/PlayLayer.cpp` может обработать frame zero внутри
resetLevel. `bot/updater.cpp` имеет собственный счётчик кадров. Поэтому запись
наблюдает очередь, сохраняет фактическое игровое время и проверяет постоянство
его сдвига относительно macro frame/240. `PlayerObject::m_position` используется
вместо визуальной позиции узла, которую Silicate может экстраполировать.

Нативная реализация processOptionsTrigger в открытых bindings отсутствует.
Исправление обычного dual без runtime-вмешательства пока не доказано; P1-only
режим — диагностическая гипотеза. Удалены старые runtime-поправки и HFTRACE1.
Запись не симулирует уровень, а получает координаты во время реального прогона.

Исследовано 23 сентября 2026. Документация docs.geode-sdk.org открывалась поисковым
инструментом с 403; использован официальный репозиторий этой документации, а
сигнатуры сверены с фактическим SDK 5.10.1 и bindings 2.2081.

## Источники

- [Silicate](https://git.silicate.dev/silicate/silicate/), commit
  `f001e01e10` (main на момент исследования): `src/replay/system.cpp`,
  `src/replay/system.hpp`, `mod.json`. Запись v3, fallback чтения v2,
  metadata v2: seed + reserved[56], целевые GD 2.2081 / Geode 5.10.1.
- [libslc](https://git.silicate.dev/silicate/slc), commit
  `402fd62984c3176a70d3e886bfa058eaca92f81f`:
  `include/slc/formats/v2.hpp`, `v3/replay.hpp`, `v3/metadata.hpp`,
  `v3/atom.hpp`, `v3/section.hpp`, `v3/action.hpp`, `v3/builtin.hpp`.
- [Geode SDK 5.10.1](https://github.com/geode-sdk/geode/tree/v5.10.1):
  `ui/Popup.hpp`, `utils/file.hpp`, `utils/async.hpp`, `loader/Mod.hpp`.
  В этом SDK Popup уже без шаблонных параметров, file picker возвращает
  `arc::Future<PickResult>`, callback держит `async::TaskHolder`.
- [Geode bindings 2.2081](https://github.com/geode-sdk/bindings/blob/7f6c2a75742856de88dad354e576dcff8a28e881/bindings/2.2081/GeometryDash.bro):
  GameOptionsTrigger, LevelEditorLayer, EditorPauseLayer, UndoObject,
  GJBaseGameLayer, PlayerObject; `bindings/include/Geode/Enums.hpp`.
- [Официальная документация](https://docs.geode-sdk.org):
  [репозиторий](https://github.com/geode-sdk/docs), commit
  `e7dac379c333d590e0f68a3917c528ad6c54b790`,
  `tutorials/modify.md`, `tutorials/popup.md`, `mods/settings.md`.
- [G.js constants](https://github.com/g-js-api/G.js/blob/249b13374114fb5c27018e4ae887320474f4d896/constants.js):
  ID Options 2899, Reverse 1917, TimeWarp 1935, Arrow 2900,
  Gameplay Offset 2901, Teleport 3022, Player Control 1932.
- [Официальный build action](https://github.com/geode-sdk/build-geode-mod/tree/e26ece68d56a3cfa0a7f790d8120c965008c3d08):
  `sdk`, `target: Win64`, `bindings-ref`, `export-pdb`, output `build-output`.

## .slc v3

Поля читаются little-endian для Win64-файлов Silicate, без reinterpret_cast
на упакованные структуры. Формат не JSON и не список абсолютных координат.

| Часть | Представление |
| --- | --- |
| Header | 8 байт ASCII `SLC3RPLY` |
| Размер metadata | uint16, сейчас 64 |
| Metadata | double TPS, uint64 seed, uint32 version/build/randomnessAlgorithm, reserved[36] |
| Atom | uint32 ID + uint64 tagged size; верхний байт — flags, нижние 56 бит — длина |
| Action atom ID 1 | uint64 число развёрнутых действий + секции |
| Footer | один байт `0xCC` |

Секции имеют 16-битный заголовок. Старшие два бита определяют input/repeat/special.
Для player input хранятся delta frame, button, P2 и down. Ширина packed state:
1, 2, 4 или 8 байт. Button 0 (Swift) разворачивается в Jump-down и Jump-up
одного кадра; button 1/2/3 — Jump/Left/Right. Repeat хранит кластер и степень
двойки количества повторений. Special передаёт Restart/RestartFull/Death,
смену TPS или Bugpoint. Frame считается накоплением дельт.

Нулевая длина Action atom поддерживается по текущей реализации upstream:
число событий определяет его конец. Неизвестные атомы пропускаются по длине.
Ненулевые flags у Action atom, несколько Action atoms и неизвестная версия
metadata выше 2 отклоняются: нельзя угадывать их смысл.

## .slc v2

Header `SILL`, double TPS, uint64 metaSize, metadata, uint64 inputCount,
uint64 blobCount. Каждый descriptor содержит uint64 width/start/count.
После всех descriptors идут packed inputs; TPS-ввод дополнительно хранит double.
Footer `EOM`. Состояние хранит delta в битах 5+, тип в битах 2–4,
P2 в бите 1, down в бите 0.

Silicate применяет metadata 64 байта, первые 8 — seed. Поддерживаются также
seed-only 8 байт и отсутствие metadata. Незнакомая длина не интерпретируется.

## Что подтверждено и что требует игры

В исходниках подтверждены wire format, API и численные значения tri-state
полей Options. Выбор `press → Off`, `release → On` следует из hold-механики
пользователя и назначения этих полей. Гарантировать точные игровые края по
одним заголовкам нельзя: порядок input/physics/trigger, стартовый кадр,
непрерывное удержание, прыжковые буферы и float-координаты требуют испытания.

Сопоставление с временной картой редактора — инженерное приближение.
Оно не воспроизводит систему random, timestep, trajectory и checkpoint fix
Silicate. В отчёте сохранены данные для диагностики этой границы, а не скрытое
обещание эквивалентности всех макросов.

Никакие исходники сторонних библиотек не включены в поставляемый проект;
парсер написан отдельно на основе формата. SDK и инструменты Actions
подтягиваются из официальных репозиториев при сборке.

## Привязка записи, beta.3 — 25 сентября 2026

LevelIdentity канонизирует порядок пар key/value внутри каждой записи, не
переставляя сами объекты. Числа X/Y/rotation нормализуются строковыми операциями
без округления float; прочие значения сохраняются. Поля editor layer 20/61
подтверждены pinned bindings GameObject; kS39 — страница цветов в
`gd.py/gd/api/header.py`. Эти три поля не участвуют в идентичности пути.
Неизвестные поля и неоднозначные записи не игнорируются.

UI сохраняет выбранный макрос отдельно от решения о применимости траектории.
Одинаковый объект GJGameLevel или совпадающий зафиксированный saved payload
позволяет показать прежнюю сессию при возврате. Это не отменяет проверку хеша
содержимого перед генерацией. Новый ключ кэша level-v1 отделён от старого
побайтового ключа; legacy HFTRACE3 принимается только при точном старом хеше.

Конкретное изменённое поле в пользовательском сохранении пока неизвестно:
нового Export logs не было. Регрессия скрытия макроса подтверждена исходниками;
новые логи показывают свойство, если сохраняется несовпадение содержимого.
