# Native dual path implementation notes (0.2.1-beta.1)

This version implements an experimental generator, not a proof that ordinary
dual is solved. No GD process or Windows toolchain was available during development.

- Continuous HFTRACE3 capture observes processQueuedButtons pre/post phases at
  240 TPS. Cache validation checks identity, complete tail, sample continuity,
  finite values, forms, sizes and consistency with input-edge snapshots.
- The generator uses native object 2064 (unlinked contact teleportal), checked
  against TeleportPortalObject/GameObjectType::TeleportPortal in the running
  GD build before editor mutation. It does not use Teleport Trigger 3022 or
  assume that property 200 targets the second icon for arbitrary objects.
- Portal scale .5; Hide 135; No Effects 116; No Particles 507; Ignore X 352;
  Static Force enabled 345, value 346 = 0; gravity mode 354 = 0. Final inert
  markers are object 1 with NoTouch 121. Working portals keep collisions enabled.
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
