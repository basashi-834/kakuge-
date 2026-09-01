# Audio/

No sound assets are bundled. `UI\AudioHelper.ps1` looks for these files
and silently no-ops if missing, so gameplay never depends on audio
existing:

| file        | played when                    |
|-------------|----------------------------------|
| attack.wav  | a fighter starts any move        |
| hit.wav     | an attack lands (not blocked)    |
| block.wav   | an attack is guarded             |
| ko.wav      | a fighter's HP reaches 0         |

**Format note**: `System.Media.SoundPlayer` (the only sound API available
without installing anything extra) only plays uncompressed **.wav**
files - no .mp3/.ogg. Drop matching `.wav` files here and they start
playing automatically, no code changes required.
