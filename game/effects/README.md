# effects/

Effect *scenes* (HitEffect.tscn, Projectile.tscn) live under `scenes/` and
their scripts under `scripts/core/`, following the same scene+script
pairing convention as the rest of the project (see `ui/README.md`).

This folder is where real VFX assets (particle textures, spritesheets for
hit sparks, etc.) go once you replace the placeholder procedural drawing in
`scripts/core/HitEffect.gd`. `HitEffect.play(kind)` already distinguishes
"hit" / "heavy_hit" / "guard" / "special" / "super" - point each case at a
texture/AnimatedSprite2D from here instead of `_draw()` and nothing else in
the combat code needs to change.
