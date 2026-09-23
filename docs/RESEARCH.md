# Research notes — 0.1.4

Проверено 23 сентября 2026.

- Silicate: https://git.silicate.dev/silicate/silicate/
  В replay hook события текущего replay frame ставятся через `queueButton(...)`, после
  чего вызывается штатный `GJBaseGameLayer::processQueuedButtons(...)`. Поэтому
  HoldForge записывает фактический `handleButton` и границы `processCommands`, а не
  выводит macro frame из `m_currentProgress`.
- Geode bindings GD 2.2081:
  https://github.com/geode-sdk/bindings/blob/7f6c2a75742856de88dad354e576dcff8a28e881/bindings/2.2081/GeometryDash.bro
  Используются `processCommands`, `processOptionsTrigger`, `handleButton`,
  `toggleDualMode`, `LevelEditorLayer::onPlaytest/onStopPlaytest/playerTookDamage` и
  поля GameOptionsTrigger 165/199.
- Geode SDK 5.10.1 / build action: workflow закрепляет SDK 5.10.1, Win64 и указанный
  bindings commit.

`previous-step-midpoint` — инженерная фаза 0.1.4: trigger помещается внутри реально
пройденного P1 X-интервала непосредственно перед фактическим input edge. Это устраняет
переменный drift `posForTime()` и общий X-offset, но правильность именно штатной
Options-семантики при непрерывном hold/dual должна быть подтверждена matrix-test в GD.
