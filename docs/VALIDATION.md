# HoldForge 0.1.4 validation status

## Completed without Geometry Dash

- Parsed/validated `mod.json` and `.github/workflows/build.yml`.
- Core parser/planner/HFTRACE2 tests pass with CTest.
- The same core tests pass under AddressSanitizer + UndefinedBehaviorSanitizer.
- `git diff --check` passes.
- Production-source scan contains no `pushButton`, `releaseButton`, `NativeOptionsScope`, `applyHoldGate`, or shared-dual runtime fix.
- Production code does not hook `EditorPauseLayer`.
- Automatic trace binds a recording to both the SLC fingerprint and current serialized level fingerprint, records actual input order and pre/post command-step positions, and only writes a complete trace after a no-damage completed editor attempt.

## Must still be tested in Geometry Dash 2.2081

This environment cannot launch Geometry Dash, so these are **not claimed as verified**:

- exact native Options-trigger phase for press/release around the recorded command step (the recorder now records whether input arrived inside `processCommands` or between steps, but does not pretend that this alone proves stock trigger ordering);
- ordinary dual behavior with one shared stream mirrored to native P1/P2 controls across dual entry/exit;
- independent P1/P2 behavior in actual 2 Player Mode;
- speed portals, short presses, orbs, dash/time-warp cases listed in `IN_GAME_TESTS.md`;
- final acceptance test: save the generated level, disable HoldForge and Silicate, then complete it by ordinary holding in stock gameplay.

If any of those fail, enable `debug-runtime`, reproduce once, and use **Export logs**. The JSON is diagnostic-only; the runtime hooks do not modify input or Options behavior.
