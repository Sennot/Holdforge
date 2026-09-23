# HoldForge 0.1.6 — Silicate `.slc` → stock-GD hold level

Target: Geometry Dash 2.2081, Geode SDK 5.10.1, Windows x64.

## Главное изменение 0.1.6

0.1.3 runtime-bridge удалён полностью. HoldForge больше не подменяет `processOptionsTrigger`,
не вызывает `PlayerObject::pushButton/releaseButton` и не чинит dual во время игры.
Сгенерированный уровень содержит только штатные Options Trigger (ID 2899) и должен
проверяться с **отключённым HoldForge**.

Точная генерация теперь по умолчанию требует автоматически записанный `.hftrace`.
Trace привязан к FNV-отпечаткам конкретного `.slc` и исходной строки уровня, к режиму
2 Player Mode, а также проверяет полный порядок press/release. Неполная попытка,
смерть, изменение уровня, другой макрос или другой режим отклоняются.

## Использование

1. Открой копию исходного уровня без ранее созданных control-Options и импортируй `.slc` через кнопку **HF**.
2. Нажми **Record trace**. Запусти Silicate replay с самого начала в editor playtest и дай ему полностью пройти уровень.
3. После успешного завершения рядом с макросом появится одноимённый `.hftrace`. Снова открой HF и импортируй тот же `.slc`.
4. Статус должен быть **Automatic trace verified**. Нажми **Create**. Пачка создаётся одной Undo-операцией; перед вставкой сохраняется backup.
5. Сохрани копию уровня, **отключи HoldForge и Silicate playback**, запусти стандартную GD и держи P1 постоянно. В настоящем 2 Player Mode держи оба ввода.

Если шаг 5 не проходит, уровень ещё не готов к публикации: с включённым HoldForge
воспроизведи проблему один раз и нажми **Export logs**. Runtime trace включён по умолчанию
и только наблюдает; получится один JSON.

## Как размещаются триггеры

Макро-кадр не выводится из `m_currentProgress`. HoldForge сопоставляет фактически
полученные `handleButton` jump press/release по порядку с событиями макроса и проверяет
`m_levelTime` против `frame / TPS`. Для каждого совпавшего edge сохраняются реальный X/Y
игрока и состояние dual непосредственно в момент входа. Trigger X берётся из измеренного
P1 X этого input-edge: общего `-0.25` и глобального drift-offset нет.

0.1.6 намеренно не требует `processCommands` для принятия trace: на предоставленном
0.1.5 журнале этот hook не дал фазовый сегмент, хотя все 412/412 `handleButton` edges
совпали с SLC и шли по строго возрастающему X. Reverse, teleport и rotated gameplay
по-прежнему отклоняются; нативную Options-фазу всё равно нужно подтвердить тестом в GD.

## Dual

- Обычный dual (`2 Player Mode = off`): это один логический input stream. Его состояние
  зеркалируется в properties **165/199**, чтобы штатно gate-ить оба PlayerObject; зеркальный
  P2 stream из Silicate допускается только если он идентичен P1 и отдельно не калибруется.
- Настоящий `2 Player Mode`: P1/P2 независимы, properties **165/199** генерируются отдельно.
  Для записи trace GD-настройка `Flip 2-Player Controls` должна быть выключена, иначе запись отклоняется.
- Поддержка этих вариантов считается подтверждённой только после игрового matrix-test,
  описанного в `docs/IN_GAME_TESTS.md`; в этой среде GD не запускается.

Tri-state Options: `1` = блокировать, `-1` = разрешить, `0` = не менять.

## Сборка GitHub Actions

Загрузи содержимое проекта в корень репозитория и запусти **Actions → Build HoldForge**.
Workflow сначала выполняет core-tests, затем собирает Win64 на Geode 5.10.1 с
закреплёнными bindings GD 2.2081 и выгружает `.geode` + `.pdb` в `HoldForge-Win64`.

Локально доступны только независимые тесты ядра:

```sh
cmake -S . -B build-tests -DHOLDFORGE_CORE_ONLY=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

## Ограничения

Classic only; 240 TPS для автоматического trace; Restart/Death/Bugpoint и platformer
inputs отклоняются. Static `posForTime()` fallback оставлен только как явно
экспериментальная настройка и по умолчанию выключен. Backup, конфликт существующих
control-Options, проверка созданных объектов и единый Undo сохранены. В
`EditorPauseLayer` мод не вмешивается.
