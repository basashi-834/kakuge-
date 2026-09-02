# DEVLOG

Running record of non-obvious bugs, decisions, and constraints for this project,
so future work doesn't re-discover (and re-spend tokens re-discovering) the same
things. Update this file whenever a fix or decision here would otherwise get
forgotten and re-hit later. Keep entries short and specific: what broke, why,
and the fix - not a full narrative.

## Project identity (read this first)

This project has **always** been C++ (Win32 + GDI+), living in `cpp-game/`.
It is a from-scratch C++ port of an earlier PowerShell/WinForms prototype
(`winforms-game/`) - **it has never been Godot**, at any point in this
project's history. If asked to "rebuild from Godot to C++," that request is
based on a mistaken premise and needs to be clarified with the user rather
than acted on literally.

Rendering pipeline: every custom-drawn screen renders into a genuine fixed
384x224 low-res `Gdiplus::Bitmap` (`platform/Layout.h`'s `VirtualW`/
`VirtualH`), which is then nearest-neighbor-upscaled onto the real window
(`App::OnPaint`). This is what makes the whole game read as real pixel art
at any output resolution - it is a deliberate, explicit design choice (see
"Photo-sprite art was removed" below), not a limitation to route around.

## Standing constraints

- **Never `git push` without the user's explicit, current-turn permission**
  ("pushして"/"OK"/"公開して"/"GitHubに上げて" or equivalent - see
  `~/.claude/CLAUDE.md` for the full house rule, which applies to every
  project, not just this one). Local `git commit` is fine anytime work is
  complete enough to checkpoint. Every reply that ships a build must end by
  asking permission to push, even if a previous round's push was approved -
  approval is not standing.
- Character visual size, hurtbox, hitbox, and pushbox are **intentionally
  separate** values (user's explicit design principle) - never assume the
  drawn image size implies the hit-detection box size, and don't wire them
  together "for simplicity."
- Photo-sprite character art (`data/images/fighter_*.png`) was explicitly
  **removed** earlier in the project at the user's request, in favor of a
  hand-drawn procedural pixel-art line-art humanoid (`DrawHumanoid` in
  `platform/Draw.cpp`). Later in the project the user asked for real
  bitmap sprite art back (confirmed explicitly: not just more procedural
  pose variety) - see "Sprite art system" below for how that request was
  reconciled with the earlier decision (art layers *on top of*, doesn't
  replace, the line-art renderer). **This session has no image-generation
  tool** - actual pixel-art image files still need to come from the user
  (or another tool), not from Claude.

## Sandbox / testing environment gotchas

- The Wine+Xvfb sandbox used for visual smoke-testing is **unreliable
  across turns**: each new environment/session frequently starts with no
  Xvfb running at all (`DISPLAY=:99` fails until `Xvfb :99 -screen 0
  1650x1080x24 &` is (re)started), and even when the display is up, clicks
  on **native Win32 child controls** (the Character Editor's real
  HWND-based buttons/combos/edits) frequently don't register via
  `xdotool`, while clicks on the custom GDI+-drawn menu screens (Title/
  Select/etc., hit-tested in `App::OnLButtonDown`) reliably do. Don't burn
  a lot of turns re-attempting native-control clicks that aren't landing -
  a screenshot of the *current* state is still useful for layout checks
  even when interaction is unreliable; fall back to careful code reading +
  hand computation (see the camera zoom/headroom math below) when
  interactive verification isn't available, and say so plainly rather than
  claiming untested things work.
- Native engine tests (`./build_linux_tests.sh`) build/run on Linux with no
  Wine dependency and are fast/reliable - run them after every engine
  change regardless of whether Wine verification is possible.
- `./build_windows.sh` cross-compiles with mingw-w64; run it after every
  platform-layer change even when Wine can't be used to visually confirm
  the result, since it still catches real compile errors.

## Fixed bugs worth remembering

- **Owner-draw `WM_DRAWITEM` must translate by `rcItem.left/top`.**
  `dis->hDC`'s origin does *not* reset to (0,0) per item - for a standalone
  button/static control that coincides with (0,0) anyway (so it "worked"),
  but a combobox's *dropdown list* shares one DC across every visible row,
  and each row's real position is `rcItem.top` (row N at
  `itemHeight*N`, not 0). Drawing everything at local (0,0) without
  translating first stacks every open-dropdown row on top of each other at
  the list's top instead of one per line. Fix (already applied, see
  `Editor_OnDrawItem` in `platform/Editor.cpp`): call
  `g.TranslateTransform(rcItem.left, rcItem.top)` once at the top of the
  handler, before any drawing.
- **`EN_CHANGE` reentrancy corrupts multi-field syncs.** Setting an EDIT
  control's text via `SetWindowTextW` fires `EN_CHANGE` synchronously. If a
  `SyncXFieldsFromDraft()`-style function sets 4 related fields (X/Y/W/H)
  one at a time, each intermediate `SetEditDouble` re-triggers the
  `WM_COMMAND` handler, which reads whatever mix of already-updated and
  not-yet-updated field text is currently on screen and writes it back
  into the live data - corrupting it mid-sync. A value-copy source isn't
  enough by itself (self-heals by the last field *in theory*, but proved
  unreliable in practice). Real fix: a module-level guard flag
  (`g_SyncingBoxFields` in `platform/Editor.cpp`) set around the whole
  Sync body; `ApplyXFieldsToDraft()` checks it and no-ops immediately if
  set, so reentrant calls during a sync are fully inert rather than
  partially-correct.
- **`CB_SETCURSEL` doesn't reliably repaint under Wine.** A real Windows
  install repaints an owner-draw combo's closed selection field
  immediately; this sandbox's Wine often doesn't. Fix: `SetComboSel()`
  helper in `platform/Editor.cpp` that also calls
  `InvalidateRect`+`UpdateWindow` after `CB_SETCURSEL`. Also, `WM_DRAWITEM`
  fires with `itemID == -1` when redrawing the closed field (not a real
  list row) - fall back to `CB_GETCURSEL` in that case.
- **Editor screen transition was slow: full control teardown/rebuild on
  every visit.** `EnterEditor()`/`LeaveEditor()` used to destroy all ~70
  native controls and recreate them from scratch on every single visit to
  the Character Editor. Fix: create once (guarded by
  `if (ComboCharacter) return;` in `CreateEditorControls()`, unchanged),
  and have `LeaveEditor()` call a new `HideEditorControls()` (just
  `ShowWindow(SW_HIDE)` on everything) instead of `DestroyEditorControls()`;
  `EnterEditor()` re-shows via the existing `ShowCreateCharacterPrompt()`/
  `HideCreateCharacterPrompt()` plus explicit `ShowWindow(SW_SHOW)` for
  `BtnBack`/`BtnLanguage` (the two controls not covered by either list).
  `DestroyEditorControls()` itself is now only called from `App::Shutdown()`.
- **Camera max-zoom must be bounded vertically, not just horizontally.**
  A naive "zoom to fit the horizontal distance" formula can produce a zoom
  where the character's on-screen height exceeds the canvas height (head
  goes off the top through the HUD) even though the horizontal framing
  looks reasonable - vertical clearance (canvas height minus top-HUD
  reserve minus character height at that zoom) is the real constraint on
  `kCameraMaxZoom`, not the horizontal distance-to-fit math. See
  `kCameraMaxZoom`'s comment in `platform/Draw.cpp` for the exact
  computation this project uses.
- **Camera center must be clamped to stage bounds, accounting for the
  current zoom's visible width** - the raw midpoint of the two fighters
  is always within stage bounds on its own, but the *visible window*
  around that midpoint (width = `VirtualW/zoom`) isn't automatically. Not
  clamping it means cornering an opponent shows a lot of wasted empty
  stage past them instead of a tight, wall-close corner shot. Fix:
  `ClampCameraCenter()` in `platform/Draw.cpp`, applied to the *target*
  center before lerping (in both `UpdateCamera` and `ResetCamera`).
- **`CharState::Block` is overloaded for two different things** - real
  post-hit blockstun (a hit sets a real `BlockstunTimer > 0`) *and* the
  idle "holding crouch+back, nothing incoming" guard-ready stance
  (`HandleGroundMovement` enters `Block` for this too, leaving
  `BlockstunTimer` at whatever it last was - typically 0). The state
  handler used to always count `BlockstunTimer` down and drop to `Idle`
  once it hit 0, with no distinction - so entering the guard-ready stance
  (`BlockstunTimer` already <= 0) dropped to `Idle` on the very next tick,
  which (still holding guard) immediately re-entered `Block` via
  `HandleGroundMovement`, which dropped to `Idle` again next tick, forever
  - a same-frame state flip loop visible as the character's state
  thrashing wildly while just holding crouch-guard with no attack
  incoming. Fixed in `Fighter::HandleStateLogic`'s `CharState::Block` case:
  only actually counts down real blockstun (`BlockstunTimer > 0`); with no
  real blockstun active, stays parked in `Block` for as long as guard
  input is held and only leaves for `Idle` once it's actually released.

## World-scale constants (see `engine/Constants.h`'s `StageConstants`)

Stage/character/camera sizing was re-derived from a user-supplied
1920x1080-proportioned reference spec (stage 3200px, ground y=920,
character height 760px/70%, start x=1250/1850) while keeping the 384x224
canvas unchanged - by ratio, not literal pixel values. All the derivation
math and current constant values are documented in `StageConstants` and in
`kCharScale`/`OriginY`'s comments in `platform/Draw.h`/`.cpp`. Notable
follow-on consequence: making the character taller (63% -> 70% of canvas
height) meant the *existing* jump arc (tuned for the old, smaller
character) no longer fit under the HUD without clipping - `ryu.json`'s
`jumpVelocity` was reduced (-254.35 -> -210) to restore a comfortable
margin; this is character-data, so it's re-tunable per-character from the
Character Editor's own JUMP VELOCITY/GRAVITY fields without touching code.

**Later update - character shrunk to ~half size.** After the above sizing
landed, the user asked to shrink the character to about half its size
(unrelated to the HUD reskin below - a separate, simpler ask). Changed
`kCharScale` in `platform/Draw.cpp` from `1.43` to `0.715` (exactly half -
idle height ~79px/35% of the 224px canvas, down from ~156px/70%), and
scaled `Fighter::PushboxHalfWidth`/`PushboxHalfHeight` (`engine/Fighter.h`)
and `HurtboxSet`'s `Stand`/`Crouch`/`Air` rects (`engine/Boxes.h`) down by
the same 0.5 factor so hit detection still visually lines up with the
smaller body - consistent with how those boxes were scaled *up* ~10%
alongside the earlier size increase. Also updated the VS-screen silhouette
sizing literal in `platform/Screens.cpp` (`156.44` -> `79.22`, matching the
new `108*kCharScale+2`) and `StageConstants::RefPlayerHeight` in
`engine/Constants.h` (`760` -> `380`) so that documentation stays in sync
per its own stated intent. **Deliberately NOT touched**: per-move attack
hitbox reach (`data/moves/ryu/*.json`) - halving those too would be a much
bigger, separate balance-tuning pass the user didn't ask for, and the
"coordinates vs. visuals are separate" principle above means normals'
reach not shrinking in lockstep with the body isn't a bug, just an
independent value; `jumpVelocity`/gravity - the smaller character now has
*more* headroom under the existing jump arc than before (shrinking only
ever loosens that constraint), so nothing needed retuning there; `OriginY`
(ground line position) and `kCameraMaxZoom` - both still safe/valid after
the shrink, just more conservative than strictly necessary now (see their
comments in `platform/Draw.h`/`.cpp`), left alone since retuning either
wasn't requested and isn't broken.

**Later update - precise 384x224-native size/layout spec.** The user then
gave a third, much more precise spec, this time expressed *directly* in
384x224 canvas terms (not scaled from a 1920x1080 reference) - explicit
CHARACTER_VISUAL_HEIGHT (88px/39.3%), CHARACTER_VISUAL_WIDTH (55px/14-15%),
GROUND_Y (189px/84.4%), PLAYER1_START_X/PLAYER2_START_X (screen x=116/268,
the canvas's 30%/70% points, ~150px apart), HUD_HEIGHT (~30-32px) and
SUPER_GAUGE_HEIGHT (~18px), explicitly asking these be treated as the
game's unified baseline going forward, not auto-adjusted. Implemented:
- `kCharScale` (`platform/Draw.cpp`) -> `88.0/108.0` (idle height exactly
  88px/39.3%).
- `OriginY` (`platform/Draw.h`) -> `189.0` (was `200.0`).
- `StageConstants::Player1StartX`/`Player2StartX` (`engine/Constants.h`)
  -> `-76.0`/`76.0` (symmetric around world X=0, 152 units apart) and
  `kCameraPaddingWorld` (`platform/Draw.cpp`) -> `232.0`, chosen together
  so the round-start camera sits at *exactly* zoom 1.0
  (`384/(152+232)==1.0`), which puts the two fighters' zoom-1.0 screen
  projection at exactly canvas x=116/268 - verified by pixel-sampling a
  Wine screenshot (head centers landed at ~506/1161 screen px against a
  measured ~508/1164 expected, ground line at ~y188 against expected 189).
- Bottom super-gauge bar (`DrawHUD` in `platform/Draw.cpp`) thickened
  6px->8px with adjusted margins so its total footprint (label to bottom
  margin) is 18px, matching SUPER_GAUGE_HEIGHT's recommendation.
- `Fighter::PushboxHalfWidth`/`PushboxHalfHeight`, `HurtboxSet`'s
  `Stand`/`Crouch`/`Air`, and the VS-screen silhouette sizing literal in
  `platform/Screens.cpp` were all rescaled proportionally to the new
  `kCharScale`, same pattern as the previous size change.
- `StageConstants`' documentation block was rewritten to explain the
  history across all three specs (1920x1080-derived -> "about half" ->
  this native-384 one) rather than silently drifting from an increasingly
  inaccurate original comment.

**Attempted and reverted: independent width scale.** To also hit the
55px CHARACTER_VISUAL_WIDTH target (which a single uniform `kCharScale`
can't do at the same time as the height target - 55/88 = 0.625 is far
"wider for its height" than this stick figure's own natural proportions),
tried adding a second horizontal-only scale (`kCharWidthScale =
55.0/24.0`) applied to `DrawHumanoid`'s static body-frame X-offsets (hip/
shoulder half-width, foot stance spread, torso width) while leaving limb
articulation/reach, line thickness and head radius on the original
height-based scale. Visually verified via a Wine screenshot and it looked
*wrong*, not just imprecise: the torso stretched into a flat, box-like
rectangle (as wide as the entire leg stance) rather than reading as a
body, because this procedural line-art humanoid's limbs are constant-
width single strokes with no independent "mass" to widen - unlike real
sprite art, there's no torso fill/shading to carry extra width, so
stretching the coordinates alone just makes the wireframe rectangular.
**Reverted entirely** (back to single `s = heightScale*kCharScale` used
everywhere, as it always was) rather than shipping a broken-looking
character - see `kCharScale`'s comment in `platform/Draw.cpp` for a
pointer back to this entry. Getting a genuine ~55px-wide silhouette
without that distortion needs an actual shape/stroke-weight redesign
(thicker limbs, real torso mass, i.e. closer to real sprite art than this
thin-line stick figure) - flagged as a follow-up rather than attempted
blind a second time; current on-screen width is whatever this stick
figure's existing (much narrower) proportions produce at the new height.

## Sprite art system (`platform/Sprites.h`/`.cpp`)

Layers optional bitmap sprite art *on top of* the procedural line-art
renderer rather than replacing it - any pose without a sprite file falls
back to the existing `DrawHumanoid` line art, so the game works with zero
art present and characters can gain real art one pose at a time. Files:
`Data/sprites/<charId>/<pose>.png` (`stand_0..3`, `crouch`, `punch`,
`kick`, `jump`, `jumppunch`, `jumpkick`, `hitstun`, `knockdown` - see
`data/sprites/README.txt` for the full convention). Loaded once per
character id and cached (`GetCharacterSprites`/`ClearSpriteCache`);
`DrawFighter` now takes `baseDataDir`/`userDir` params to reach that cache
and picks a sprite via `PickSprite()` before falling through to the
existing line-art `switch`. Standing idle is a 4-frame loop keyed off
`Fighter::FrameCounter % 4` (ticks up every simulation frame, never
resets) - `kIdleTicksPerFrame` in `Sprites.h` controls how many ticks each
frame holds (1 = literally every tick/60fps, per the user's explicit
spec - flagged as likely reading like flicker rather than a breathing
loop once real art lands, easy one-constant fix if so). Sprites are
mirrored for `facing < 0` via GDI+'s 3-point `DrawImage` overload
(swapping the top-left/top-right destination points), not a separate
flipped asset. Block/Throw/WakeUp/Dead intentionally have no sprite slot
(outside the user's requested pose list) and always use line art.

## HUD visual reskin (Capcom-style reference screenshots)

User asked to bring the in-match HUD's visual *style* (not just position/
size, which was already handled by the world-scale rework above) closer to
two SFIII-Third-Strike/CvS2-style reference screenshots (ornate/metallic
beveled HP bars, small character portrait busts at the bar's outer end).
What was matched vs. deliberately not attempted, and why:

- **Matched**: bar now has a `DrawGlossCap` top-sheen plus a two-tone
  beveled frame (2px outer `borderColor` + 1px translucent-white inner
  hairline) in `DrawBar()` (`platform/Draw.cpp`) instead of a single flat
  border - both `DrawHPBar`/`DrawGaugeBar` inherit it since they delegate
  to `DrawBar`. A small portrait bust box (`DrawPortraitBust`) was added at
  the bar's outer corner (screen-edge side), with the bar itself shrunk
  and shifted inward to make room (`portraitSize=22, portraitGap=3`;
  `barW` 140->115; P1 bar now starts at canvas x=27 instead of x=3, P2
  mirrored) - matches the reference layout's "portrait-then-bar"
  composition. Verified visually via Wine/Xvfb screenshot + pixel-zoomed
  crops (see "Sandbox gotcha" note below - the first verification attempt
  was actually looking at a stale pre-rebuild `Kakuge.exe` still running
  from earlier in the session; had to `kill` it and relaunch to see the
  real result).
- **NOT attempted, on purpose**: the portrait bust is a **generic
  procedural placeholder** (silhouette head + red headband + shoulders,
  same shape for every character, colored from `pal.Accent`) - not a real
  per-character portrait illustration. This session has no image-
  generation tool (same limitation already noted for sprite art above);
  real portraits would need actual art files supplied by the user, at
  which point `DrawPortraitBust` should be swapped for a `g.DrawImage`
  call keyed off the fighter's character id, following the exact
  load/cache pattern `Sprites.h`/`.cpp` already established. Also not
  attempted: move-list side panels and round-win pip indicators seen in
  the reference images - round-win state isn't currently tracked/rendered
  anywhere in `BattleSystem`, so pips would need new state, not just a
  drawing change; flagged as a possible future follow-up, not done here.
- **Sandbox gotcha reconfirmed**: a Wine process from earlier in the same
  session was still running when a new verification pass started later -
  it was executing the *old* pre-rebuild `.exe`, so the first screenshot
  looked like none of the day's edits had taken effect. Always check
  `ps aux | grep Kakuge` and compare the running process's start time
  against the `.exe`'s mtime (or just unconditionally `pkill` any stale
  instance) before trusting a Wine screenshot as reflecting the latest
  build.

## Known follow-ups (flagged, not yet acted on)

- CPU AI's per-move `EffectiveRange` values (55-100 world units) weren't
  rescaled when the stage widened from 260 to 640 world units - CPU
  engagement-distance judgment may now be off. Out of scope for the sizing
  request that prompted the world-scale rework; flagged for a future pass.
- **In-game (custom GDI+) Character Editor UI** was requested (replace the
  native Win32-controls editor screen with one drawn/hit-tested the same
  way Title/Select/etc. are) but not started - `platform/Editor.cpp` is
  ~1300 lines of native-control wiring and this is a large enough rewrite
  that rushing it in the same turn as other work risks a broken result;
  treat as its own dedicated pass. Whatever replaces it still needs to
  cover everything the current native editor does (stats, moves, hitbox/
  hurtbox authoring incl. drag-to-resize, JP/EN toggle, character
  creation) - review this file's Editor.cpp-related entries above first.
