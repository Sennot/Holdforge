> Исторические заметки до beta.4. Описанные здесь запреты теперь предупреждения; актуальное поведение см. README.md.

# Native dual path implementation notes (0.2.2-beta.1)

This version implements an experimental generator, not a proof that ordinary
dual is solved. No GD process or Windows toolchain was available during development.

- Continuous HFTRACE3 capture observes processQueuedButtons pre/post phases at
  240 TPS. Cache validation checks identity, complete tail, sample continuity,
  finite values, forms, sizes and consistency with input-edge snapshots.
- The generator uses native object 2902 (unlinked BLUE contact teleportal), checked
  against TeleportPortalObject/GameObjectType::TeleportPortal in the running
  GD build before editor mutation. It does not use Teleport Trigger 3022 or
  assume that property 200 targets the second icon for arbitrary objects.
- Portal scale .5; Hide 135; No Effects 116; No Particles 507; Ignore X 352;
  Static Force enabled 345, value 346 = 0; gravity mode 354 = 0. Final inert
  markers are unlinked ORANGE exits 2064 with NoTouch 121. Working portals keep collisions enabled.
- Native target resolution is grounded in pinned Geode bindings
  `bindings/2.2081/inline/GJBaseGameLayer.cpp`: getPortalTarget looks up a group;
  getPortalTargetPos returns target position for IDs other than legacy 747.
  This permits each next disabled portal to be the unique destination marker.
  Both group membership and references are reserved before generation.
- Toggle 1049 / target 51 / enabled 56: every portal has initial off at X=0,
  on inside a recorded crossing, off in the next crossing. Adjacent on/off
  triggers affect different groups; no dependence on same-group equal-X ordering.
- Native portal bounds are measured using a detached GameObject prototype.
  The generator conservatively rejects intersections with the swept P1 box.
  It does not silently omit a dangerous interval or assume a P2 collision filter.
- All created object IDs, coordinates, flags, groups and targets are checked
  after deserialization. Failure rolls back the batch; success gets one Undo entry.
- Verify checks every dual step, form, activation player, target Y, preserved X,
  complete helper coverage and physical continuous hold. It never injects input
  or edits native physics. P2 tolerance 6 is a test threshold, not an error bound
  proven for the construction. Exact rotation/gravity/orb state is not reproduced.

Open gameplay risks to validate: trigger vs collision phase, instant Toggle
latency, changed camera/portal state, chained teleport cooldown, orb-triggered
side effects, linked dual gravity, spider jumps, form changes and overlapping
paths. The generator accepts eight form IDs; the 64 synthetic pair tests only
validate planning and serialization. They cannot resolve these engine risks.

Record and Verify must be run from the full original and generated levels
respectively. Save and Exit followed by normal Play is supported without
restarting GD. Old input-only caches are deliberately not reused.

## ID correction, 2026-09-24

The initial implementation confused entrance and exit. The primary object-name
registry [Tinker resources/objects.csv](https://github.com/Alphalaneous/Tinker/blob/main/resources/objects.csv)
identifies 2902 = Unlinked Blue Teleport Portal, 2064 = Unlinked Orange Teleport
Portal, 2065 = Custom Particles. The registry was fetched and inspected directly.
`NativeObjects.hpp` is the shared source for generation and editor checks.
The running game still validates RTTI, object type, absence of linked exit and
`m_isYellowPortal == false` before generation. No type override is applied.

Manual dual mode uses recorded boundaries to suppress interior macro gates,
enables both controls at entry and restores accumulated macro state at exit.
An input exactly at entry is consumed under manual hold; an input exactly at
exit contributes to the restored state. It creates no auto helpers. Precise
trajectory Verify is explicitly unavailable for manual batches; use normal Play.
