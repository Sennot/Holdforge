# In-game acceptance matrix — 0.1.4

Эти проверки обязательны перед заявлением поддержки. В среде разработки без GD они
**не выполнены**.

## A. Native Options MRE

Минимальный level: ровный участок, один Options `165=1` в начале, затем `165=-1`,
затем `165=1`; property 199 = 0. Запуск без Silicate/HoldForge: держать jump с самого
начала. Логировать только наблюдение штатного поведения: момент изменения
`m_controlsDisabled`, `m_holdingButtons`, `m_jumpBuffered`, фактический jump/release.
Повторить для cube/ship/wave/ball/UFO/robot/spider/swing и для входа/выхода ordinary dual.
Никакой hook не должен менять эти состояния.

## B. Source trace

Для нового `.slc`: Import → Record trace → полный Silicate replay от старта до complete.
Проверить, что `.hftrace` отвергается после любого из действий: изменение level string,
смена 2 Player Mode, другой macro, смерть, остановка до complete, лишний/пропущенный
press/release.

## C. Final publication test

После Create сохранить отдельную копию. Полностью отключить HoldForge и Silicate replay.
Проверить обычным постоянным удержанием:

- solo без dual;
- ordinary dual enter/exit (`2 Player Mode = off`, один общий stream зеркалируется в 165/199);
- true 2 Player Mode с независимыми P1/P2;
- speed portal до/после короткого press/release;
- обычный orb на press и release около соседних physics steps;
- dash/timewarp, если trace принял уровень.

Критерий: прохождение совпадает с source macro без runtime-мода. Любая смерть =
поддержка данного случая не подтверждена; экспортировать один diagnostic JSON.
