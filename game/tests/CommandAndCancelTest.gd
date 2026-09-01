extends Node
## Deterministic checks for two features that are easy to silently break:
##   - CommandParser motion recognition (section 24, "236" etc, with some
##     leniency rather than a frame-exact match)
##   - Normal -> special cancel chains (section 21/22): starting a new move
##     mid-attack should only succeed inside its cancelStartFrame/EndFrame
##     window, and only into a move listed in cancelRoutes.
##
## Run with: godot --headless --path . res://tests/CommandAndCancelTest.tscn

const FIGHTER_SCENE := preload("res://scenes/Fighter.tscn")

var failures: Array[String] = []
var passed := 0

func _ready() -> void:
	_test_command_parser()
	await _test_cancel_window()
	print("[CMD/CANCEL TEST] %d passed, %d failed" % [passed, failures.size()])
	for f in failures:
		printerr("[CMD/CANCEL TEST] FAIL: ", f)
	get_tree().quit(0 if failures.is_empty() else 1)


func _check(label: String, cond: bool) -> void:
	if cond:
		passed += 1
	else:
		failures.append(label)
	print("[CMD/CANCEL TEST] %s -> %s" % [label, "OK" if cond else "FAIL"])


func _test_command_parser() -> void:
	# Simulate a player rolling 2 -> 3 -> 6 then pressing Special, each held
	# a couple frames, exactly like CPUController's own motion feed.
	var buf := InputBuffer.new()
	var f := 0
	for digit in [2, 2, 3, 3, 6]:
		f += 1
		buf.record_frame(f, digit, [])
	f += 1
	buf.record_frame(f, 6, ["Special"])
	_check("236+Special recognized", CommandParser.matches(buf, "236", "Special"))
	_check("214 NOT recognized from a 236 buffer", not CommandParser.matches(buf, "214", "Special"))

	# Button pressed too late (outside the grace window) should not count.
	var buf2 := InputBuffer.new()
	f = 0
	for digit in [2, 3, 6]:
		f += 1
		buf2.record_frame(f, digit, [])
	for i in range(20): # let the motion age out of both the match window and button grace
		f += 1
		buf2.record_frame(f, 5, [])
	f += 1
	buf2.record_frame(f, 5, ["Special"])
	_check("236+Special NOT recognized when button comes too late", not CommandParser.matches(buf2, "236", "Special"))


func _test_cancel_window() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	var fighter: Fighter = FIGHTER_SCENE.instantiate()
	get_tree().root.add_child(fighter)
	await get_tree().process_frame
	var stats := DataManager.get_character("ryu")
	var moves := DataManager.get_moveset("ryu")
	fighter.setup(stats, moves, false)
	fighter.opponent = fighter # harmless self-reference, avoids null checks elsewhere

	var neutral := {"left": false, "right": false, "down": false, "up": false,
		"buttons_held": {"Light": false, "Medium": false, "Heavy": false, "Special": false, "Super": false}}

	# Frame 1: press Light -> starts standing_light (startup 4, active 3,
	# recovery 7; cancelStartFrame=4, cancelEndFrame=10 per moves/ryu/standing_light.json).
	fighter.set_virtual_input(_with_button(neutral, "Light"))
	fighter.frame_step(1.0 / 60.0)
	_check("Light press starts standing_light", fighter.state_machine.current_move == "standing_light")

	# Hold neutral for 4 more frames to reach current_frame=4 - exactly
	# standing_light's cancelStartFrame (moves/ryu/standing_light.json).
	fighter.set_virtual_input(neutral)
	for i in range(4):
		fighter.frame_step(1.0 / 60.0)
	_check("still in standing_light at cancel window open, frame=%d" % fighter.state_machine.current_frame,
		fighter.state_machine.current_move == "standing_light" and fighter.state_machine.current_frame == 4)

	# Now press Heavy - standing_light.cancelRoutes includes standing_heavy,
	# so this should cancel immediately into it instead of waiting out
	# standing_light's recovery.
	fighter.set_virtual_input(_with_button(neutral, "Heavy"))
	fighter.frame_step(1.0 / 60.0)
	_check("Heavy cancels standing_light into standing_heavy inside the window",
		fighter.state_machine.current_move == "standing_heavy" and fighter.state_machine.current_frame == 0)


func _with_button(base: Dictionary, button: String) -> Dictionary:
	var d := base.duplicate(true)
	d.buttons_held[button] = true
	return d
