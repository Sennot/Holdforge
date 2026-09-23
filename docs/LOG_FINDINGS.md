# Findings carried into 0.1.6

`m_currentProgress` is diagnostic only and is not treated as the Silicate frame. A single global X/time offset is not used.

The supplied 0.1.5 report showed the failure was `TRACE_PHASE` at matched=0: no `processCommands` movement segment was available. The same report still contained all 412/412 real jump edges in `handleButton`; their press/release order matched the SLC exactly, their `m_levelTime` matched `frame / 240`, and P1 X was monotonic.

0.1.6 therefore records HFTRACE3 from the actual `handleButton` input edge and uses editor Stop as explicit successful finalization. Death, extra/missing/reordered edges, timing mismatch, level/macro mismatch and non-monotonic X still reject the trace.
