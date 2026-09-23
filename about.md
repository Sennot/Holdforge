# HoldForge

Import Silicate `.slc` v2/v3 macros and convert jump press/release events to native
Options Triggers in the level editor. Open **HoldForge** using the **HF** button
on the left side of the editor while playtest is stopped.

Includes dual/shared input, independent two-player streams, a hold timeline preview,
a single undo entry per import, raw level backups and exportable debug reports.

This initial version uses the editor's time map. Test the result in-game before
saving. It is not a full replay physics simulator. Strict mode blocks known
mechanics that cannot be reliably mapped by a static timeline.
