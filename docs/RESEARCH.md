# Research notes — 0.1.6

Target: GD 2.2081 / Geode 5.10.1. Silicate playback ultimately reaches GD input handling through queued buttons; HoldForge observes the resulting native `GJBaseGameLayer::handleButton` without injecting input.

The 0.1.5 diagnostic run established `handleButton` as the reliable synchronization point for this editor workflow: all 412 macro edges were observed in order, with `level_time == frame / 240` within a tiny floating-point error, while the attempted `processCommands` phase capture was absent. HFTRACE3 therefore stores the actual input-edge time and position instead of rejecting on a missing command-step bracket.

Native Options behavior and exact publication-safe activation phase still require in-game tests with HoldForge disabled after generation.
