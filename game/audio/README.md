# audio/

No sound assets are bundled. `scripts/autoload/AudioManager.gd` looks for
these files and silently no-ops if they're missing, so gameplay never
depends on audio existing:

| key      | file                    | played when                       |
|----------|-------------------------|------------------------------------|
| attack   | `res://audio/attack.ogg`| a fighter starts any move          |
| hit      | `res://audio/hit.ogg`   | an attack lands (not blocked)      |
| block    | `res://audio/block.ogg` | an attack is guarded                |
| ko       | `res://audio/ko.ogg`    | a fighter's HP reaches 0            |

Drop `.ogg`/`.wav` files with these exact names in this folder and they
start playing automatically - no code changes required.
