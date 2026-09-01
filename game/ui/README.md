# ui/

UI *scenes* live under `scenes/` (Title.tscn, Game.tscn, Result.tscn,
CharacterEditor.tscn) and UI *scripts* live under `scripts/ui/` - this
matches Godot's convention of keeping a node's `.tscn` and `.gd` easy to
find together per feature, which is why this folder doesn't duplicate them.

This folder is reserved for shared UI-only assets that aren't tied to one
screen - e.g. a common `Theme` resource (fonts/button styles) if you add one
later. Drop a `.tres` Theme here and set it as `Root`'s Theme property in
each screen's scene to reskin the whole UI in one place.
