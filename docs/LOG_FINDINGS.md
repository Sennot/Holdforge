# Findings carried into 0.1.4

Предыдущие журналы показали, что `m_currentProgress` в одном конкретном прогоне имел
отношение `2*(frame+1)`, но это не считается универсальной формулой. Разность фазы
Options и исходных input-событий также была переменной, поэтому один time/X offset
не является корректным решением.

0.1.3 уменьшала ошибки runtime-вмешательством (`processOptionsTrigger` + явные
push/release), из-за чего публикация требовала HoldForge. В 0.1.4 этот механизм удалён.

Новая диагностика сохраняет macro actions/gates, фактический input, P1/P2 состояние,
координаты предыдущего physics step до/после, рассчитанный trigger X, native Options
before/after, dual transitions, deaths, настройки и версии модов. Автоматический
HFTRACE2 сохраняется только после полного clean editor attempt.
