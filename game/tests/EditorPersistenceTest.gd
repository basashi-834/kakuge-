extends Node
## Verifies the Character Editor actually persists edits to user:// JSON
## and that DataManager picks the override back up after a reload -
## section 4 "編集内容は保存できるようにしてください" / section 31.
##
## Run with: godot --headless --path . res://tests/EditorPersistenceTest.tscn

const EDITOR_SCENE := preload("res://scenes/CharacterEditor.tscn")

var failures: Array[String] = []
var passed := 0

func _check(label: String, cond: bool) -> void:
	if cond:
		passed += 1
	else:
		failures.append(label)
	print("[EDITOR TEST] %s -> %s" % [label, "OK" if cond else "FAIL"])

func _ready() -> void:
	await get_tree().process_frame
	await get_tree().process_frame

	# Clean slate: remove any previous user:// override so this test is
	# reproducible.
	var override_path := "user://characters/ryu.json"
	if FileAccess.file_exists(override_path):
		DirAccess.remove_absolute(override_path)

	var editor := EDITOR_SCENE.instantiate()
	get_tree().root.add_child(editor)
	await get_tree().process_frame
	await get_tree().process_frame

	_check("editor loaded ryu as current character", editor.current_char_id == "ryu")
	_check("editor loaded a move", editor.current_move_id != "")

	var original_hp: float = editor.stat_fields["maxHP"].value
	var new_hp := original_hp + 250.0
	editor.stat_fields["maxHP"].value = new_hp
	editor.stat_fields["name"].text = "RYU-TEST"

	var original_damage: float = editor.move_fields["damage"].value
	var new_damage := original_damage + 33.0
	editor.move_fields["damage"].value = new_damage

	editor._on_save_pressed()

	_check("user:// character override file was written", FileAccess.file_exists(override_path))

	# Reload DataManager from disk (simulates a fresh game launch) and make
	# sure the edit actually persisted, not just held in the editor's memory.
	DataManager.reload_all()
	var reloaded_stats := DataManager.get_character("ryu")
	_check("reloaded maxHP matches the edit", reloaded_stats.max_hp == int(new_hp))
	_check("reloaded name matches the edit", reloaded_stats.name == "RYU-TEST")

	var reloaded_move := DataManager.get_move("ryu", editor.current_move_id)
	_check("reloaded move damage matches the edit", reloaded_move.damage == int(new_damage))

	print("[EDITOR TEST] %d passed, %d failed" % [passed, failures.size()])
	for f in failures:
		printerr("[EDITOR TEST] FAIL: ", f)
	get_tree().quit(0 if failures.is_empty() else 1)
