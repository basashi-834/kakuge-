extends Node
## Headless regression check for the core fight loop - no display needed.
## Instances Game.tscn directly under the tree root (Player1 idle - no
## keyboard in headless mode - vs the CPU) and watches for real combat
## events (hits landing, HP dropping, a KO) so a broken build fails loudly
## in a terminal/CI instead of only "looking fine" in the editor.
##
## Run with:
##   godot --headless --path . --fixed-fps 600 res://tests/HeadlessSmokeTest.tscn
##
## Note: this test instances Game.tscn directly (rather than going through
## GameManager.goto_game()) because this script's own node IS the running
## main scene, and change_scene_to_file() would free it out from under
## itself. Title -> Game -> Result screen transitions are each a single
## get_tree().change_scene_to_file() call (see GameManager.gd) exercised
## manually / by the Windows build, not re-tested here.

const GAME_SCENE := preload("res://scenes/Game.tscn")

var frame_count := 0
const MAX_FRAMES := 12000 # 200s of game-time safety cap
var match_controller: MatchController
var saw_hitbox_active := false
var saw_hp_drop := false
var saw_ko := false

func _ready() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	print("[TEST] boot: instancing Game.tscn")
	match_controller = GAME_SCENE.instantiate()
	get_tree().root.add_child(match_controller)
	await get_tree().process_frame
	await get_tree().process_frame
	_hook()

func _hook() -> void:
	print("[TEST] MatchController ready: %s vs %s" % [match_controller.player1.stats.name, match_controller.player2.stats.name])
	match_controller.player1.hp_changed.connect(func(cur, mx): _on_hp("P1", cur, mx))
	match_controller.player2.hp_changed.connect(func(cur, mx): _on_hp("P2/CPU", cur, mx))
	match_controller.match_ended.connect(_on_match_ended)
	set_process(true)

func _on_hp(who: String, cur: int, mx: int) -> void:
	saw_hp_drop = true
	print("[TEST] %s HP -> %d/%d" % [who, cur, mx])

func _process(_delta: float) -> void:
	if match_controller == null or match_controller.player1 == null or match_controller.player2 == null:
		return
	frame_count += 1
	if match_controller.player2.hitbox.is_active:
		saw_hitbox_active = true
	if frame_count % 300 == 0:
		var p1 := match_controller.player1
		var p2 := match_controller.player2
		print("[TEST] t=%ds P1hp=%d(x=%.0f) P2hp=%d(x=%.0f) dist=%.0f P1st=%s/%s P2st=%s/%s P1g=%.0f P2g=%.0f" % [
			frame_count / 60, p1.current_hp, p1.global_position.x, p2.current_hp, p2.global_position.x,
			abs(p1.global_position.x - p2.global_position.x),
			Constants.state_name(p1.state_machine.current_state), p1.state_machine.current_move,
			Constants.state_name(p2.state_machine.current_state), p2.state_machine.current_move,
			p1.gauge.value, p2.gauge.value,
		])
	if frame_count > MAX_FRAMES:
		_finish(false, "timed out waiting for KO/timeout")

func _on_match_ended(winner: Fighter, is_draw: bool) -> void:
	saw_ko = true
	print("[TEST] match_ended winner=%s draw=%s" % [(winner.stats.name if winner else "none"), is_draw])
	_finish(saw_hitbox_active and saw_hp_drop, "")

func _finish(ok: bool, reason: String) -> void:
	if ok:
		print("[TEST] SMOKE TEST PASSED (hitbox_active=%s hp_drop=%s ko=%s)" % [saw_hitbox_active, saw_hp_drop, saw_ko])
		get_tree().quit(0)
	else:
		push_error("[TEST] SMOKE TEST FAILED %s (hitbox_active=%s hp_drop=%s ko=%s)" % [reason, saw_hitbox_active, saw_hp_drop, saw_ko])
		get_tree().quit(1)
