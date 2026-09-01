# assets/sprites/

No art assets yet - characters/projectiles are drawn procedurally
(rectangles/circles) by `scripts/core/FighterVisual.gd` and
`scripts/core/ProjectileVisual.gd` so the fighting-game systems could be
built and verified first (per the project brief: "まずゲームシステムが正しく
動くことを最優先").

To add real sprites later: drop spritesheets/textures here, replace the
`_draw()` calls in FighterVisual.gd with an `AnimatedSprite2D`/`Sprite2D`
driven by `fighter.state_machine.current_state` (and `current_move` for
attack-specific frames) - no other gameplay code needs to change, since
combat logic never reads the visual layer.
