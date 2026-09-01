extends Node
## Deterministic unit test for the guard rule table (section 14):
##   High     -> blockable standing or crouching
##   Low      -> blockable crouching only
##   Overhead -> blockable standing only
##   Throw    -> never blockable
## Exercises Fighter.receive_hit()/_check_guard() directly rather than
## relying on CPU behavior, so results don't depend on AI randomness.
##
## Run with: godot --headless --path . res://tests/GuardRulesTest.tscn

const FIGHTER_SCENE := preload("res://scenes/Fighter.tscn")

var attacker: Fighter
var defender: Fighter
var failures: Array[String] = []
var passed := 0

func _ready() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	attacker = FIGHTER_SCENE.instantiate()
	defender = FIGHTER_SCENE.instantiate()
	get_tree().root.add_child(attacker)
	get_tree().root.add_child(defender)
	await get_tree().process_frame

	var stats := DataManager.get_character("ryu")
	var moves := DataManager.get_moveset("ryu")
	attacker.setup(stats, moves, false)
	defender.setup(stats, moves, false)
	attacker.opponent = defender
	defender.opponent = attacker
	attacker.global_position = Vector2(-40, 0)
	defender.global_position = Vector2(40, 0)
	attacker.facing = Constants.FACING_RIGHT
	defender.facing = Constants.FACING_LEFT

	_case("High vs standing block -> BLOCKED", "standing_medium", false, true)
	_case("High vs crouching block -> BLOCKED", "standing_medium", true, true)
	_case("Low vs crouching block -> BLOCKED", "crouch_light", true, true)
	_case("Low vs standing block (should NOT block) -> HIT", "crouch_light", false, false)
	_case("Overhead vs standing block -> BLOCKED", "jump_attack", false, true)
	_case("Overhead vs crouching block (should NOT block) -> HIT", "jump_attack", true, false)
	_case("Throw vs standing block (never blockable) -> HIT", "standing_throw", false, false)
	_case("Throw vs crouching block (never blockable) -> HIT", "standing_throw", true, false)
	_case("High vs no-guard-input (not holding back) -> HIT", "standing_medium", null, false)

	print("[GUARD TEST] %d passed, %d failed" % [passed, failures.size()])
	for f in failures:
		printerr("[GUARD TEST] FAIL: ", f)
	get_tree().quit(0 if failures.is_empty() else 1)


func _case(label: String, move_id: String, crouch_guard, expect_blocked: bool) -> void:
	defender.current_hp = defender.stats.max_hp
	defender.hitstun_timer = 0
	defender.blockstun_timer = 0
	defender.is_crouching_guard = false
	if crouch_guard == null:
		defender.state_machine.change_state(Constants.CharState.IDLE)
		defender.set_virtual_input({"left": false, "right": false, "down": false, "up": false,
			"buttons_held": {"Light": false, "Medium": false, "Heavy": false, "Special": false, "Super": false}})
	elif crouch_guard:
		# defender faces LEFT (toward the attacker) - "back" is RIGHT.
		defender.state_machine.change_state(Constants.CharState.CROUCH)
		defender.is_crouching_guard = true
		defender.set_virtual_input({"left": false, "right": true, "down": true, "up": false,
			"buttons_held": {"Light": false, "Medium": false, "Heavy": false, "Special": false, "Super": false}})
	else:
		defender.state_machine.change_state(Constants.CharState.BLOCK)
		defender.set_virtual_input({"left": false, "right": true, "down": false, "up": false,
			"buttons_held": {"Light": false, "Medium": false, "Heavy": false, "Special": false, "Super": false}})

	var move: MoveData = attacker.moveset[move_id]
	var result: Dictionary = defender.receive_hit(move, attacker)
	var ok: bool = result.get("blocked", false) == expect_blocked
	if ok:
		passed += 1
	else:
		failures.append("%s (got blocked=%s)" % [label, result.get("blocked", false)])
	print("[GUARD TEST] %s -> %s" % [label, "OK" if ok else "FAIL"])
