extends RefCounted
class_name CPUController
## CPU opponent "brain" - deliberately separate from Fighter (section 27/38:
## "CPU AIをCharacterから分離する"). Reads public state off both fighters,
## returns a synthetic input dict that Fighter consumes exactly like real
## keyboard input via set_virtual_input(). Never touches Fighter internals
## directly.

var self_fighter: Fighter
var opponent: Fighter
var rng := RandomNumberGenerator.new()

var _pending_sequence: Array[Dictionary] = []
var _current_input: Dictionary = _neutral()
var _decision_cooldown: int = 0

const CLOSE_RANGE := 80.0 # tuned to sit inside normal-attack reach (~76-90px)
const MID_RANGE := 420.0
const ANTI_AIR_RANGE := 260.0
const LOW_HP_RATIO := 0.25


func _init(p_self: Fighter, p_opponent: Fighter) -> void:
	self_fighter = p_self
	opponent = p_opponent
	rng.randomize()


static func _neutral() -> Dictionary:
	return {"left": false, "right": false, "down": false, "up": false,
		"buttons_held": {"Light": false, "Medium": false, "Heavy": false, "Special": false, "Super": false}}


func decide() -> Dictionary:
	if not _pending_sequence.is_empty():
		return _pending_sequence.pop_front()

	if self_fighter.is_dead or opponent.is_dead:
		return _neutral()

	# Reactive guard: if the opponent is mid-attack and close, sometimes hold
	# back regardless of the current "plan" (section 27 defensive reaction).
	var dx := opponent.global_position.x - self_fighter.global_position.x
	var dist: float = absf(dx)
	var dir_to_opp := 1 if dx >= 0 else -1

	if opponent.state_machine.current_state == Constants.CharState.ATTACK and dist < 160.0 \
			and self_fighter.state_machine.is_actionable() and rng.randf() < 0.7:
		return _hold_back(dir_to_opp)

	if _decision_cooldown > 0:
		_decision_cooldown -= 1
		return _current_input

	_current_input = _plan(dist, dir_to_opp)
	_decision_cooldown = rng.randi_range(8, 18)
	return _current_input


func _plan(dist: float, dir_to_opp: int) -> Dictionary:
	if not self_fighter.state_machine.is_actionable() and self_fighter.state_machine.current_state != Constants.CharState.JUMP:
		return _neutral()

	var low_hp := self_fighter.current_hp < self_fighter.stats.max_hp * LOW_HP_RATIO
	var opponent_airborne := opponent.state_machine.current_state == Constants.CharState.JUMP

	# Anti-air: opponent jumping in range -> use an AntiAir-tagged move.
	if opponent_airborne and dist < ANTI_AIR_RANGE:
		var anti_air := _find_move(func(m): return m.has_tag(Constants.TAG_ANTI_AIR))
		if anti_air != null:
			return _use_move(anti_air, dir_to_opp)

	# Spend a full super gauge when the opportunity is there (section 25).
	if dist < 300.0 and rng.randf() < 0.5:
		var super_move := _find_move(func(m): return m.has_tag(Constants.TAG_SUPER) and self_fighter.gauge.can_spend(m.meter_cost))
		if super_move != null:
			return _use_move(super_move, dir_to_opp)

	if dist < CLOSE_RANGE:
		if low_hp and rng.randf() < 0.5:
			return _hold_back(dir_to_opp)
		if rng.randf() < 0.55:
			var atk := _pick_close_attack()
			if atk != null:
				return _use_move(atk, dir_to_opp)
		return _move_dir(dir_to_opp) # keep tightening spacing so attacks actually reach

	elif dist < MID_RANGE:
		if low_hp and rng.randf() < 0.35:
			return _move_dir(-dir_to_opp)
		if rng.randf() < 0.6:
			return _move_dir(dir_to_opp)
		return _neutral()

	else: # far range
		if not low_hp and rng.randf() < 0.6:
			var projectile := _find_move(func(m): return m.has_tag(Constants.TAG_PROJECTILE))
			if projectile != null:
				return _use_move(projectile, dir_to_opp)
		return _move_dir(dir_to_opp)


func _pick_close_attack() -> MoveData:
	var pool: Array = []
	for m in self_fighter.moveset.values():
		if m.has_tag(Constants.TAG_NORMAL) and m.stance == "stand" and not m.has_tag(Constants.TAG_THROW):
			pool.append(m)
	if pool.is_empty():
		return null
	return pool[rng.randi() % pool.size()]


func _find_move(predicate: Callable) -> MoveData:
	var pool: Array = []
	for m in self_fighter.moveset.values():
		if predicate.call(m):
			pool.append(m)
	if pool.is_empty():
		return null
	return pool[rng.randi() % pool.size()]


func _move_dir(dir: int) -> Dictionary:
	var input := _neutral()
	var facing := self_fighter.facing
	if dir == facing:
		input.right = facing == 1
		input.left = facing == -1
	else:
		input.right = facing == -1
		input.left = facing == 1
	return input


func _hold_back(dir_to_opp: int) -> Dictionary:
	var input := _neutral()
	var facing := self_fighter.facing
	var back_is_right := facing == -1
	input.right = back_is_right
	input.left = not back_is_right
	if rng.randf() < 0.3:
		input.down = true
	return input


func _use_move(move: MoveData, dir_to_opp: int) -> Dictionary:
	if move.input_command == "":
		var input := _neutral()
		input.buttons_held[move.button] = true
		return input
	var digits: Array = CommandParser.MOTIONS.get(move.input_command, [])
	if digits.is_empty():
		return _neutral()
	_pending_sequence.clear()
	for d in digits:
		var raw := _digit_to_raw(d, self_fighter.facing)
		_pending_sequence.append(raw)
		_pending_sequence.append(raw.duplicate(true))
	var final_raw := _digit_to_raw(digits.back(), self_fighter.facing)
	final_raw = final_raw.duplicate(true)
	final_raw.buttons_held = final_raw.buttons_held.duplicate(true)
	final_raw.buttons_held[move.button] = true
	_pending_sequence.append(final_raw)
	_pending_sequence.append(_neutral())
	return _pending_sequence.pop_front()


func _digit_to_raw(digit: int, facing: int) -> Dictionary:
	var input := _neutral()
	var forward_is_right := facing == 1
	var forward := digit in [3, 6, 9]
	var back := digit in [1, 4, 7]
	var down := digit in [1, 2, 3]
	var up := digit in [7, 8, 9]
	if forward:
		input.right = forward_is_right
		input.left = not forward_is_right
	elif back:
		input.right = not forward_is_right
		input.left = forward_is_right
	input.down = down
	input.up = up
	return input
