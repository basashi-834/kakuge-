extends CharacterBody2D
class_name Fighter
## The character controller. Owns physics + delegates to small focused
## helpers (StateMachine, MoveExecutor, InputBuffer, CommandParser,
## SuperGauge) rather than doing everything itself (section 38).
##
## IMPORTANT: gameplay logic does NOT run in Godot's automatic
## _physics_process. MatchController calls frame_step() explicitly, once
## per fixed 60Hz tick, for player 1 then player 2, so hit/pushbox
## resolution always happens in a deterministic, frame-exact order
## (section 38: "GameManagerとCharacterを分離する").

signal hp_changed(current: int, max_hp: int)
signal ko(fighter: Fighter)
signal effect_requested(kind: String, world_pos: Vector2)
signal sound_requested(kind: String)
signal projectile_spawn_requested(move: MoveData, spawn_pos: Vector2, facing: int, owner_fighter: Fighter)

const GROUND_Y := 0.0
const DASH_INPUT_WINDOW := 14
const DASH_DURATION := 14
const KNOCKDOWN_FRAMES := 40
const HARD_KNOCKDOWN_FRAMES := 60
const WAKEUP_FRAMES := 14
const THROWN_LOCK_FRAMES := 20

@onready var hitbox: Hitbox = $Hitbox
@onready var hurtbox: Hurtbox = $Hurtbox
@onready var pushbox: Pushbox = $Pushbox
@onready var visual: Node2D = $Visual

var stats: CharacterStats
var moveset: Dictionary = {}          # move_id -> MoveData
var state_machine: StateMachine = StateMachine.new()
var input_buffer: InputBuffer = InputBuffer.new()
var gauge: SuperGauge = SuperGauge.new()

var current_hp: int = 1000
var facing: int = Constants.FACING_RIGHT
var facing_locked: bool = false
var is_player_controlled: bool = true
var is_dead: bool = false
var opponent: Fighter = null
var key_map: Dictionary = {}
var stage_min_x: float = -560.0
var stage_max_x: float = 560.0

var current_move_data: MoveData = null
var projectile_spawned_this_activation: bool = false

var hitstun_timer: int = 0
var blockstun_timer: int = 0
var hitstop_timer: int = 0
var knockdown_timer: int = 0
var wakeup_timer: int = 0
var thrown_timer: int = 0
var dash_timer: int = 0
var is_crouching_guard: bool = false
var frame_counter: int = 0

var _held_buttons_prev: Dictionary = {}
var _last_forward_tap_frame: int = -999
var _virtual_input: Dictionary = {}
var _debug_last_hit_result: Dictionary = {}


func _ready() -> void:
	hitbox.owner_fighter = self
	hurtbox.owner_fighter = self
	pushbox.owner_fighter = self


func setup(p_stats: CharacterStats, p_moveset: Dictionary, p_is_player: bool, p_key_map: Dictionary = {}) -> void:
	stats = p_stats
	moveset = p_moveset
	is_player_controlled = p_is_player
	key_map = p_key_map
	reset_for_round()


func reset_for_round() -> void:
	current_hp = stats.max_hp
	is_dead = false
	current_move_data = null
	hitstun_timer = 0
	blockstun_timer = 0
	hitstop_timer = 0
	knockdown_timer = 0
	wakeup_timer = 0
	thrown_timer = 0
	dash_timer = 0
	gauge.value = 0.0
	input_buffer.clear()
	state_machine.change_state(Constants.CharState.IDLE)
	hitbox.deactivate()
	hp_changed.emit(current_hp, stats.max_hp)


## Used by CPUController to feed synthesized input instead of real keys.
func set_virtual_input(input: Dictionary) -> void:
	_virtual_input = input


# ---------------------------------------------------------------------
# Main per-frame entry point (called by MatchController)
# ---------------------------------------------------------------------
func frame_step(delta: float) -> void:
	if is_dead:
		velocity.y += stats.gravity * delta
		position += velocity * delta
		if position.y > GROUND_Y:
			position.y = GROUND_Y
			velocity.y = 0.0
		return

	frame_counter += 1
	var raw := _read_input()
	var digit := InputBuffer.compute_digit(raw.left, raw.right, raw.up, raw.down, facing)
	var pressed := _newly_pressed_buttons(raw.buttons_held)
	# Synthetic "Throw" input: Light+Medium held together (no dedicated throw
	# key in the section 29 control scheme), a common fighting-game shortcut.
	if raw.buttons_held.get("Light", false) and raw.buttons_held.get("Medium", false) \
			and ("Light" in pressed or "Medium" in pressed) and not pressed.has("Throw"):
		pressed.append("Throw")
	input_buffer.record_frame(frame_counter, digit, pressed)

	if hitstop_timer > 0:
		hitstop_timer -= 1
		return # fully frozen: no state progression, no movement

	state_machine.tick()
	_handle_state_logic(raw, pressed)
	_apply_physics(delta)
	_clamp_to_stage()
	_update_hurtbox_stance()
	_update_facing()


# ---------------------------------------------------------------------
# Input
# ---------------------------------------------------------------------
func _read_input() -> Dictionary:
	if is_player_controlled:
		return {
			"left": Input.is_physical_key_pressed(key_map.get("left", KEY_A)),
			"right": Input.is_physical_key_pressed(key_map.get("right", KEY_D)),
			"down": Input.is_physical_key_pressed(key_map.get("down", KEY_S)),
			"up": Input.is_physical_key_pressed(key_map.get("up", KEY_SPACE)),
			"buttons_held": {
				"Light": Input.is_physical_key_pressed(key_map.get("light", KEY_J)),
				"Medium": Input.is_physical_key_pressed(key_map.get("medium", KEY_K)),
				"Heavy": Input.is_physical_key_pressed(key_map.get("heavy", KEY_L)),
				"Special": Input.is_physical_key_pressed(key_map.get("special", KEY_U)),
				"Super": Input.is_physical_key_pressed(key_map.get("super", KEY_I)),
			}
		}
	return _virtual_input if not _virtual_input.is_empty() else {
		"left": false, "right": false, "down": false, "up": false,
		"buttons_held": {"Light": false, "Medium": false, "Heavy": false, "Special": false, "Super": false}
	}


func _newly_pressed_buttons(held: Dictionary) -> Array:
	var pressed: Array = []
	for btn in held.keys():
		if held[btn] and not _held_buttons_prev.get(btn, false):
			pressed.append(btn)
	_held_buttons_prev = held.duplicate()
	return pressed


func _is_holding_back(raw: Dictionary) -> bool:
	return (raw.left and facing == Constants.FACING_RIGHT) or (raw.right and facing == Constants.FACING_LEFT)


func _is_holding_forward(raw: Dictionary) -> bool:
	return (raw.right and facing == Constants.FACING_RIGHT) or (raw.left and facing == Constants.FACING_LEFT)


# ---------------------------------------------------------------------
# State logic
# ---------------------------------------------------------------------
func _handle_state_logic(raw: Dictionary, pressed: Array) -> void:
	match state_machine.current_state:
		Constants.CharState.HITSTUN:
			hitstun_timer -= 1
			if hitstun_timer <= 0:
				state_machine.change_state(Constants.CharState.IDLE)
			velocity.x = move_toward(velocity.x, 0.0, 900.0 * (1.0 / Constants.FPS))
		Constants.CharState.BLOCK:
			blockstun_timer -= 1
			velocity.x = move_toward(velocity.x, 0.0, 900.0 * (1.0 / Constants.FPS))
			if blockstun_timer <= 0:
				state_machine.change_state(Constants.CharState.IDLE)
		Constants.CharState.THROW:
			thrown_timer -= 1
			if thrown_timer <= 0:
				_enter_knockdown(false)
		Constants.CharState.KNOCKDOWN:
			knockdown_timer -= 1
			velocity.x = move_toward(velocity.x, 0.0, 1200.0 * (1.0 / Constants.FPS))
			if knockdown_timer <= 0:
				wakeup_timer = WAKEUP_FRAMES
				state_machine.change_state(Constants.CharState.WAKEUP)
		Constants.CharState.WAKEUP:
			wakeup_timer -= 1
			if wakeup_timer <= 0:
				state_machine.change_state(Constants.CharState.IDLE)
		Constants.CharState.ATTACK:
			_try_start_move(raw, pressed) # allowed only if within cancel window
			_progress_move()
		Constants.CharState.JUMP:
			if not _try_start_move(raw, pressed):
				velocity.x = (1 if raw.right else 0) * stats.walk_forward_speed - (1 if raw.left else 0) * stats.walk_forward_speed
			if position.y >= GROUND_Y and velocity.y >= 0:
				position.y = GROUND_Y
				velocity.y = 0
				state_machine.change_state(Constants.CharState.CROUCH if raw.down else Constants.CharState.IDLE)
		_: # IDLE / WALK_FORWARD / WALK_BACKWARD / CROUCH
			if dash_timer > 0:
				dash_timer -= 1
			if not _try_start_move(raw, pressed):
				_handle_ground_movement(raw)


func _handle_ground_movement(raw: Dictionary) -> void:
	is_crouching_guard = raw.down and _is_holding_back(raw)

	if raw.up and not raw.down:
		velocity.y = stats.jump_velocity
		state_machine.change_state(Constants.CharState.JUMP)
		velocity.x = (stats.walk_forward_speed if _is_holding_forward(raw) else (-stats.walk_forward_speed if _is_holding_back(raw) else 0.0))
		return

	if raw.down:
		if _is_holding_back(raw):
			state_machine.change_state(Constants.CharState.BLOCK)
		else:
			state_machine.change_state(Constants.CharState.CROUCH)
		velocity.x = 0.0
		return

	if _is_holding_forward(raw):
		if frame_counter - _last_forward_tap_frame <= DASH_INPUT_WINDOW and dash_timer <= 0:
			dash_timer = DASH_DURATION
		_last_forward_tap_frame = frame_counter
		var spd := stats.dash_speed if dash_timer > 0 else stats.walk_forward_speed
		velocity.x = spd * facing
		state_machine.change_state(Constants.CharState.WALK_FORWARD)
	elif _is_holding_back(raw):
		velocity.x = -stats.walk_backward_speed * facing
		state_machine.change_state(Constants.CharState.BLOCK if _will_hold_guard() else Constants.CharState.WALK_BACKWARD)
	else:
		velocity.x = 0.0
		state_machine.change_state(Constants.CharState.IDLE)


func _will_hold_guard() -> bool:
	# Standing back = guard stance the instant an opposing attack is close;
	# for simplicity we treat any "holding back while grounded" as guard-
	# ready (WALK_BACKWARD still legally blocks - see _check_guard), so
	# this only decides which VISIBLE state we show. Kept simple: only show
	# BLOCK once actually retreating away from an active/near attack.
	return opponent != null and opponent.state_machine.current_state == Constants.CharState.ATTACK


# ---------------------------------------------------------------------
# Moves
# ---------------------------------------------------------------------
func _try_start_move(raw: Dictionary, pressed: Array) -> bool:
	if pressed.is_empty():
		return false
	var stance := _current_stance()
	var candidates: Array = []
	for move in moveset.values():
		if move.input_command != "" and CommandParser.matches(input_buffer, move.input_command, move.button):
			candidates.append(move)
	for btn in pressed:
		for move in moveset.values():
			if move.input_command == "" and move.button == btn and move.stance == stance:
				candidates.append(move)
	if candidates.is_empty():
		return false
	# Priority: Super > Special > Normal
	candidates.sort_custom(func(a, b): return _move_priority(a) > _move_priority(b))
	for move in candidates:
		if _can_start(move):
			_start_move(move)
			return true
	return false


func _move_priority(m: MoveData) -> int:
	if m.has_tag(Constants.TAG_SUPER):
		return 3
	if m.has_tag(Constants.TAG_SPECIAL):
		return 2
	return 1


func _current_stance() -> String:
	if state_machine.current_state == Constants.CharState.JUMP:
		return "air"
	if state_machine.current_state == Constants.CharState.CROUCH:
		return "crouch"
	return "stand"


func _can_start(move: MoveData) -> bool:
	if move.has_tag(Constants.TAG_SUPER) or move.meter_cost > 0:
		if not gauge.can_spend(move.meter_cost):
			return false
	if state_machine.current_state == Constants.CharState.ATTACK:
		if current_move_data == null:
			return false
		return MoveExecutor.can_cancel(current_move_data, state_machine.current_frame) and current_move_data.can_cancel_into(move.id)
	return true


func _start_move(move: MoveData) -> void:
	if move.meter_cost > 0:
		gauge.spend(move.meter_cost)
	current_move_data = move
	projectile_spawned_this_activation = false
	facing_locked = true
	state_machine.change_state(Constants.CharState.ATTACK, move.id)
	sound_requested.emit("attack")


func _progress_move() -> void:
	if current_move_data == null:
		state_machine.change_state(Constants.CharState.IDLE)
		return
	var frame := state_machine.current_frame
	MoveExecutor.drive_hitbox(hitbox, current_move_data, frame, facing)
	if MoveExecutor.get_phase(current_move_data, frame) == MoveExecutor.PHASE_ACTIVE \
			and current_move_data.has_tag(Constants.TAG_PROJECTILE) and not projectile_spawned_this_activation:
		projectile_spawned_this_activation = true
		projectile_spawn_requested.emit(current_move_data, global_position, facing, self)
	if MoveExecutor.get_phase(current_move_data, frame) == MoveExecutor.PHASE_DONE:
		hitbox.deactivate()
		facing_locked = false
		var was_air := position.y < GROUND_Y - 1.0
		current_move_data = null
		if was_air:
			state_machine.change_state(Constants.CharState.JUMP)
		else:
			state_machine.change_state(Constants.CharState.IDLE)


# ---------------------------------------------------------------------
# Combat resolution (called by MatchController / CombatResolver)
# ---------------------------------------------------------------------
func is_invincible_against(kind: String) -> bool:
	if state_machine.current_state == Constants.CharState.ATTACK and current_move_data != null:
		return MoveExecutor.is_invincible(current_move_data, state_machine.current_frame, kind)
	return false


func receive_hit(move: MoveData, attacker: Fighter) -> Dictionary:
	if is_dead:
		return {"blocked": false, "whiffed": true}
	var invincible_kind := Constants.INVINCIBLE_THROW if move.guard_type == Constants.GUARD_THROW else Constants.INVINCIBLE_STRIKE
	if is_invincible_against(invincible_kind):
		return {"blocked": false, "whiffed": true}

	var blocked := false
	if move.guard_type != Constants.GUARD_THROW:
		blocked = _check_guard(move)

	hitstop_timer = move.hitstop
	if blocked:
		var chip := int(round(move.damage * move.chip_damage_percent))
		current_hp = max(0, current_hp - chip)
		blockstun_timer = move.blockstun
		state_machine.change_state(Constants.CharState.BLOCK)
		gauge.add(move.meter_gain * 0.5)
		_apply_knockback(move, attacker, true)
		effect_requested.emit("guard", global_position)
		sound_requested.emit("block")
	else:
		current_hp = max(0, current_hp - move.damage)
		gauge.add(move.meter_gain)
		_apply_knockback(move, attacker, false)
		if move.guard_type == Constants.GUARD_THROW:
			thrown_timer = THROWN_LOCK_FRAMES
			state_machine.change_state(Constants.CharState.THROW)
		elif move.hit_outcome == Constants.HIT_NORMAL:
			hitstun_timer = move.hitstun
			state_machine.change_state(Constants.CharState.HITSTUN)
		else:
			_enter_knockdown(move.hit_outcome == Constants.HIT_HARD_KNOCKDOWN, move.hitstun)
		var fx := "super" if move.has_tag(Constants.TAG_SUPER) else ("special" if move.has_tag(Constants.TAG_SPECIAL) else ("heavy_hit" if move.has_tag(Constants.TAG_HEAVY) else "hit"))
		effect_requested.emit(fx, global_position)
		sound_requested.emit("hit")

	hp_changed.emit(current_hp, stats.max_hp)
	if current_hp <= 0 and not is_dead:
		is_dead = true
		state_machine.change_state(Constants.CharState.DEAD)
		sound_requested.emit("ko")
		ko.emit(self)
	_debug_last_hit_result = {"blocked": blocked}
	return {"blocked": blocked, "whiffed": false}


func _enter_knockdown(hard: bool, custom_frames: int = 0) -> void:
	## `custom_frames` lets a move's own `hitstun` value drive how long the
	## knockdown lasts (data-driven per section 31); falls back to a fixed
	## default so moves that don't bother setting it still work.
	knockdown_timer = custom_frames if custom_frames > 0 else (HARD_KNOCKDOWN_FRAMES if hard else KNOCKDOWN_FRAMES)
	state_machine.change_state(Constants.CharState.KNOCKDOWN)


func _check_guard(move: MoveData) -> bool:
	var raw := _read_input()
	var holding_back := _is_holding_back(raw)
	var in_guard_posture := state_machine.current_state == Constants.CharState.BLOCK \
		or (state_machine.is_actionable() and holding_back)
	if not in_guard_posture or not holding_back:
		return false
	match move.guard_type:
		Constants.GUARD_HIGH:
			return true
		Constants.GUARD_LOW:
			return is_crouching_guard or state_machine.current_state == Constants.CharState.CROUCH
		Constants.GUARD_OVERHEAD:
			return not (is_crouching_guard or state_machine.current_state == Constants.CharState.CROUCH)
	return false


func _apply_knockback(move: MoveData, attacker: Fighter, is_block: bool) -> void:
	var dir := -attacker.facing
	var kx := move.knockback_x * (0.4 if is_block else 1.0)
	velocity.x = kx * dir
	if not is_block and move.knockback_y != 0.0:
		velocity.y = -abs(move.knockback_y)


# ---------------------------------------------------------------------
# Physics / misc
# ---------------------------------------------------------------------
func _apply_physics(delta: float) -> void:
	if state_machine.current_state == Constants.CharState.JUMP or position.y < GROUND_Y - 0.01:
		velocity.y += stats.gravity * delta
	else:
		velocity.y = 0.0
	position += velocity * delta
	if position.y > GROUND_Y:
		position.y = GROUND_Y
		velocity.y = 0.0


func _clamp_to_stage() -> void:
	position.x = clamp(position.x, stage_min_x, stage_max_x)


func _update_hurtbox_stance() -> void:
	if state_machine.current_state == Constants.CharState.JUMP or position.y < GROUND_Y - 0.01:
		hurtbox.set_stance("air")
	elif state_machine.current_state == Constants.CharState.CROUCH or is_crouching_guard:
		hurtbox.set_stance("crouch")
	else:
		hurtbox.set_stance("stand")


func _update_facing() -> void:
	if facing_locked or opponent == null:
		return
	if not state_machine.is_actionable():
		return
	# NOTE: deliberately does NOT flip Fighter's own scale.x. Hitbox offsets
	# (MoveExecutor.drive_hitbox) and the placeholder visual (FighterVisual)
	# already multiply by `facing` themselves - mirroring the whole node's
	# transform on top of that would double-flip every child position.
	facing = Constants.FACING_RIGHT if opponent.global_position.x >= global_position.x else Constants.FACING_LEFT


func get_move(id: String) -> MoveData:
	return moveset.get(id, null)


func debug_info() -> Dictionary:
	return {
		"state": Constants.state_name(state_machine.current_state),
		"move": state_machine.current_move,
		"frame": state_machine.current_frame,
		"hp": current_hp,
		"gauge": gauge.value,
		"velocity": velocity,
		"hitstun": hitstun_timer,
		"blockstun": blockstun_timer,
		"hitstop": hitstop_timer,
	}
