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
character facing left) and sized/cropped to its own natural pose - it gets
scaled as a whole to match the character's on-screen height, whatever the
source image's pixel dimensions are.

Un-listed states (blocking, being thrown, waking up from a knockdown,
defeated) always use the line-art renderer - they weren't part of the
requested pose set.
