Drop PNG sprite art here to replace the game's procedural pixel-art line-art
renderer, one pose at a time, per character. No sprite files are bundled by
default, so every character keeps drawing with the line-art humanoid until
you add art for it - nothing needs to be "turned on" in code.

Folder layout: sprites/<character id>/<pose>.png
  (character id = the same id used under Data/characters/<id>.json,
  e.g. "ryu")

Pose files, per character:
  stand_0.png .. stand_3.png   4-frame standing idle loop (cycles every
                                 game tick - 60 swaps/sec; if that reads as
                                 flicker rather than a breathing loop once
                                 real art is in, ask to slow the cycle down,
                                 or edit kIdleTicksPerFrame in
                                 platform/Sprites.h)
  crouch.png                   crouching
  punch.png                    grounded punch (any punch-button normal)
  kick.png                     grounded kick (any kick-button normal)
  jump.png                     airborne, not attacking
  jumppunch.png                airborne punch
  jumpkick.png                 airborne kick
  hitstun.png                  getting hit (のけぞり)
  knockdown.png                knocked down (倒れこみ)

Any file you don't provide simply falls back to the line-art pose it
replaces - you can add these incrementally in any order.

Art should be drawn facing right (the game mirrors it automatically for a
character facing left). Recommended authoring canvas, per the game's
384x224 layout spec: 75 x 90 px per frame (CHARACTER_SPRITE_WIDTH/HEIGHT),
with the body itself about 55 wide x 88 tall inside it and the feet on the
canvas's bottom row - the game scales the whole image so its height matches
the character's 88px on-screen height, whatever the source pixel size, so
padding the canvas to 75x90 mostly matters for keeping frames aligned with
each other. Draw with hard pixel edges (no anti-aliasing / soft shading);
the renderer never smooths or blurs, so soft edges would show as-is.

The sprite is visual only: collision uses separate pushbox/hurtbox/hitbox
data (Data/characters/<id>.json "hurtboxes", Data/moves/<id>/*.json
"hitbox"/"frameBoxes"), never the image's own bounds - press F1 in
Training Mode to see the boxes over the art.

Un-listed states (blocking, being thrown, waking up from a knockdown,
defeated) always use the line-art renderer - they weren't part of the
requested pose set.
