extends Node
## Autoload singleton (see project.godot [autoload]).
## Owns ALL external data I/O: character base stats + move frame data.
##
## Default data ships read-only inside the project under res://characters
## and res://moves/<id>/. The Character Editor writes edits to user://
## (an OS-writable folder outside the exported .pck) so changes survive a
## game restart without ever touching the packaged read-only files
## (section 31/section 4 "編集内容は保存できるようにしてください").
##
## Resolution order per file: user:// override > res:// default.

const CHAR_DIR_RES := "res://characters/"
const CHAR_DIR_USER := "user://characters/"
const MOVES_DIR_RES := "res://moves/"
const MOVES_DIR_USER := "user://moves/"

var _characters: Dictionary = {}       # id -> CharacterStats
var _movesets: Dictionary = {}         # char_id -> Dictionary(move_id -> MoveData)

func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(CHAR_DIR_USER)
	DirAccess.make_dir_recursive_absolute(MOVES_DIR_USER)
	reload_all()


func reload_all() -> void:
	_characters.clear()
	_movesets.clear()
	_load_characters_from(CHAR_DIR_RES)
	_load_characters_from(CHAR_DIR_USER) # overrides
	for char_id in _characters.keys():
		_movesets[char_id] = {}
		_load_moves_from(char_id, MOVES_DIR_RES.path_join(char_id))
		_load_moves_from(char_id, MOVES_DIR_USER.path_join(char_id)) # overrides


func _load_characters_from(dir_path: String) -> void:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return
	dir.list_dir_begin()
	var fname := dir.get_next()
	while fname != "":
		if not dir.current_is_dir() and fname.ends_with(".json"):
			var data: Variant = _read_json(dir_path.path_join(fname))
			if data is Dictionary and data.has("id"):
				_characters[data["id"]] = CharacterStats.from_dict(data)
		fname = dir.get_next()
	dir.list_dir_end()


func _load_moves_from(char_id: String, dir_path: String) -> void:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return
	dir.list_dir_begin()
	var fname := dir.get_next()
	while fname != "":
		if not dir.current_is_dir() and fname.ends_with(".json"):
			var data: Variant = _read_json(dir_path.path_join(fname))
			if data is Dictionary and data.has("id"):
				_movesets[char_id][data["id"]] = MoveData.from_dict(data)
		fname = dir.get_next()
	dir.list_dir_end()


func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return null
	var text := f.get_as_text()
	f.close()
	var parsed = JSON.parse_string(text)
	return parsed


func _write_json(path: String, data: Variant) -> bool:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(data, "\t"))
	f.close()
	return true


func get_character_ids() -> Array:
	return _characters.keys()


func get_character(id: String) -> CharacterStats:
	return _characters.get(id, null)


func get_moveset(char_id: String) -> Dictionary:
	return _movesets.get(char_id, {})


func get_move(char_id: String, move_id: String) -> MoveData:
	var ms: Dictionary = _movesets.get(char_id, {})
	return ms.get(move_id, null)


## Persists a character's base stats to user:// so it survives a restart.
func save_character(stats: CharacterStats) -> bool:
	_characters[stats.id] = stats
	return _write_json(CHAR_DIR_USER.path_join(stats.id + ".json"), stats.to_dict())


## Persists a single move's frame data to user://.
func save_move(char_id: String, move: MoveData) -> bool:
	if not _movesets.has(char_id):
		_movesets[char_id] = {}
	_movesets[char_id][move.id] = move
	return _write_json(MOVES_DIR_USER.path_join(char_id).path_join(move.id + ".json"), move.to_dict())
