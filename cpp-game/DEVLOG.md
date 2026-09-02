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
  `platform/Draw.cpp`). Any future request to add "character art"/sprites
  needs to clarify whether that's meant to reverse this decision (actual
  bitmap art assets - which also requires an actual image source, since
  there's no image-generation tool available in this session) or just
  wants more procedural pose variety within the existing line-art system.

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

## Known follow-ups (flagged, not yet acted on)

- CPU AI's per-move `EffectiveRange` values (55-100 world units) weren't
  rescaled when the stage widened from 260 to 640 world units - CPU
  engagement-distance judgment may now be off. Out of scope for the sizing
  request that prompted the world-scale rework; flagged for a future pass.
