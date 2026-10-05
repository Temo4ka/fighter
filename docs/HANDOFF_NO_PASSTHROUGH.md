# Handoff: no pass-through (branch feature/no-passthrough)

Paused at the user's request. Last commit is WIP: it builds (debug) but tests fail and
temporary debug hooks are still in the code (see "Must clean up").

## Task items

| # | Item | State |
|---|---|---|
| 1 | Arms yield instead of pass-through | done (rig): `unjam` limbs soften (`yieldStiffness`) and pull back to `yieldPose`, still colliding; yield ends after `yieldSec` once the clip's pose of the limb is clear of the opponent (`Rig::isWishBlocked`), or at once when the clip wish changes (a strike). Tests in tests/rig. Wall clinch scenario fails on the guard-reach check (see below). |
| 2 | Striking posed legs stop in any phase | done: `contactStopInStartup` removed; stop also at dynamic parts the solver could not push away; startup contact = jammed strike (counts as a hit with real speed, no min_reaction, no chain; `Fighter::isJammed`). `kick.json` re-authored with a chamber (knee 120, shin -130 at 0.16 s, extends to 0.28 s): contacts in the active phase from 0.80 m pelvis gap on. `low_kick.json`: chamber at 0.16, active now [0.20, 0.40] (was 0.24) - tuning item. Weapon clips pose only arms. |
| 3 | Non-striking legs spacing | mostly done: `rig::keepApart` keeps the predicted bodies apart (`Rig::predictBody`: exact next-step posed pose incl. planted feet, torso+head shifted with the pelvis, a floor "shadow" of lifted feet), bisection on the shift, capped by new `posedSeparationSpeed` (combat.json, 12 m/s). Arms and current strikers (`Rig::setStrikingParts`) excluded. |
| 4 | Global invariant test | written: tests/combat/test_no_passthrough.cpp (5 scripted 40 s fights, `Battle::findDeepestOverlap`, panel line `overlap` + Contacts mark). Fails: deepest still 2-12 cm (see status). |
| 5 | Stop only on closing contacts | done: a pair counts only if the striker's own motion in the step took it deeper (`MinClosingDepth`); limit is max(contactStopDepth, depth before step). Close-jab question: not yet checked. |
| 6 | Remove limitRateByStartup | done (commit 88822a0). |
| 7 | Docs/TUNING, presets, screenshots | not started: TUNING.md/DATA_FORMATS.md/ARCHITECTURE.md not updated; release build not run; no screenshots. |

## Status of tests (debug)

Failing: the 5 `No pass-through:*` tests; contact/scenario tests calibrated on the old kick
(`Contact: a body kick stops at the pelvis it hits`, `Contact: in the startup ...` - must be
rewritten since the switch is gone, `Battle: same input gives the same result` (no knockdown
any more: kick range), `Movement: from the switched stance ...`, `Fight: a block in the right
zone ...`, `Rig: a posed kick stops at the opponent's posed legs`, wall jab scenario
(guard reach 0.30 < 0.319: guards now collide and rest on each other at 0.66 m)).
Determinism itself holds.

Remaining overlaps (with 4 Box2D steps per tick): feet 4-7 cm (vertical/fast leg motion
the spacing cap or prediction misses), shin/thigh vs foot up to 12 cm in knockdowns
(lying body ignores posed parts - mask in `refreshCollisionMask`), arms 1.5-3 cm resting
under motor pressure (soft contacts).

## Next steps

1. Make the experiment permanent: several Box2D steps per simulation step (env
   `DBG_PASSES=4 DBG_SUBSTEPS=1` cut fast arm tunnelling from 6-10 cm to ~2 cm). Put it in
   `World::Config` (+ combat.json / TUNING if feel-relevant).
2. Remove debug hooks (below).
3. Investigate remaining feet overlaps (run with `DBG_SPACING=1` - prints spacing decisions
   and per-tick overlaps > 1 cm); knockdown legs vs lying body (consider removing the
   posed-part mask for a ragdoll now that strikers stop at dynamic parts).
4. Decide tolerance for arm-vs-arm resting contact (~2 cm achievable) - ask user.
5. Re-calibrate scenario test ranges for the new kick; rewrite the startup contact test
   (now: jammed, no pass-through); decide wall-scenario guard check (guards collide now).
6. Check close jabs at 0.5-0.63 m land (item 5).
7. Docs: TUNING.md (yieldPose/yieldStiffness/yieldSec, posedSeparationSpeed, removed
   contactStopInStartup, jam rule, kick/low_kick clips, panel `overlap`, limbs "yields"),
   DATA_FORMATS (rig `yieldPose`), ARCHITECTURE (no pass-through of arms).
8. Release build without warnings, ctest both presets, screenshots in scratch, push.

## Must clean up (temporary debug code)

- src/physics/world.cpp: `DBG_HERTZ`, `DBG_SUBSTEPS`, `DBG_PUSH`, `DBG_PASSES`, `DBG_STOP`
  getenv hooks (+ cstdio/cstdlib/string includes).
- src/rig/spacing.cpp: `DBG_SPACING` printing block.
- src/combat/battle.cpp: `DBG_SPACING` per-tick overlap print.
- tests/combat/test_no_passthrough.cpp prints reports to stderr (fine for diagnosis; maybe keep as INFO).

## Contract changes (outside rig)

physics/events.hpp `PartOverlap`; physics::World: `findDeepestOverlap`,
`isOverlappingOtherFighterAt`, `getGapAt`, `findPosedStop` lost `Holding` param, tunnel
hits (`collectTunnelHits`), manifold depth for intersecting cores; combat::Battle
`findDeepestOverlap`; rig::Rig `isYielding` (was `isUnjamming`), `setStrikingParts`,
`predictBody`, `measureGap`, `stopAtContact` lost `Holding`; rig file keys `yieldPose`,
`yieldStiffness`, `yieldSec`; combat.json `posedSeparationSpeed`, `contactStopInStartup` removed.

## Open questions for the user

- Feet in a 2D side view cannot overlap: two stances touch at ~0.75 m between pelvises,
  so fighters no longer get as close as 0.5 m (close clips under 0.6 m rarely trigger).
- Low kick startup 0.24 -> 0.20 s (needed so the extension is in the active phase).
- Jammed startup contact counts as a weak hit (decision; explained in fighter.hpp).
