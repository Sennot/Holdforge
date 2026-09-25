# HoldForge

Silicate `.slc` v2/v3 to native hold objects for GD 2.2081.

**0.2.2-beta.6:** accepts native entrance 2902 by class and role without a
false category check. Reports dual coverage and located gaps, shrinks portals
near P1, avoids reserving geometry values as groups, and retains usable helpers
when groups run out. Temporary scene exits retain Verify.

**0.2.2-beta.5:** fixes ambiguous CCPoint assignment in the Windows build.

**0.2.2-beta.4:** compatibility checks now give warnings. Damage callbacks do
not identify noclip or cancel Record/Verify. Partial recordings are retained;
missing positions use approximate editor timeline mapping. Verify differences
are reported without interrupting the run or blocking creation.

Ordinary dual uses a shared input. Invisible dual auto is an approximate native
portal path; Manual dual sections lets the author build those sections by hand.
Two Player Mode keeps independent input streams. No runtime input injection.

Import → optional Record → Analyze → Create → optional Verify.
Use Save and Exit, then normal Play; Save and Play is not required.
Export logs includes warnings and runtime observations. In-game validation is
still needed; core tests do not validate Geometry Dash physics.
