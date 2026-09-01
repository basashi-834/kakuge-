extends Node
## Autoload singleton. Owns screen flow + match configuration - kept
## completely separate from Fighter/MatchController (section 38:
## "GameManagerとCharacterを分離する"). MatchController owns the actual
## frame-by-frame fight; this just remembers what to start next and where
## to go when it ends.

const SCENE_TITLE := "res://scenes/Title.tscn"
const SCENE_GAME := "res://scenes/Game.tscn"
const SCENE_RESULT := "res://scenes/Result.tscn"
const SCENE_EDITOR := "res://scenes/CharacterEditor.tscn"

var selected_character_id: String = "ryu"
var cpu_character_id: String = "ryu"

# Best-of-N scaffold (section 35: minimal = 1 round, structured to extend).
var rounds_to_win: int = 1
var p1_round_wins: int = 0
var p2_round_wins: int = 0
var round_time_seconds: int = 99

# Result screen reads these after a match ends.
var last_winner_is_player: bool = false
var last_is_draw: bool = false

var debug_visible: bool = false


func _ready() -> void:
	_load_match_rules()


func _load_match_rules() -> void:
	const PATH := "res://data/match_rules.json"
	if not FileAccess.file_exists(PATH):
		return
	var f := FileAccess.open(PATH, FileAccess.READ)
	if f == null:
		return
	var parsed = JSON.parse_string(f.get_as_text())
	f.close()
	if parsed is Dictionary:
		round_time_seconds = int(parsed.get("roundTimeSeconds", round_time_seconds))
		rounds_to_win = int(parsed.get("roundsToWin", rounds_to_win))


func p1_key_map() -> Dictionary:
	return {
		"left": KEY_A, "right": KEY_D, "down": KEY_S, "up": KEY_SPACE,
		"light": KEY_J, "medium": KEY_K, "heavy": KEY_L,
		"special": KEY_U, "super": KEY_I,
	}


func goto_title() -> void:
	p1_round_wins = 0
	p2_round_wins = 0
	get_tree().change_scene_to_file(SCENE_TITLE)


func goto_game() -> void:
	p1_round_wins = 0
	p2_round_wins = 0
	get_tree().change_scene_to_file(SCENE_GAME)


func rematch() -> void:
	goto_game()


func goto_editor() -> void:
	get_tree().change_scene_to_file(SCENE_EDITOR)


func report_match_result(winner_is_player: bool, is_draw: bool) -> void:
	last_winner_is_player = winner_is_player
	last_is_draw = is_draw
	get_tree().change_scene_to_file(SCENE_RESULT)


func quit_game() -> void:
	get_tree().quit()


func toggle_debug() -> void:
	debug_visible = not debug_visible
