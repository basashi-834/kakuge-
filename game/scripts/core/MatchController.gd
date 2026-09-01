extends Node2D
class_name MatchController
## Per-match orchestrator attached to the root of scenes/Game.tscn.
## This is intentionally the ONLY place that calls Fighter.frame_step(),
## so hit detection / pushbox resolution / projectile updates always run
## in one deterministic order every physics tick (60Hz - section 11/38).
## The Game scene is reloaded fresh for every new match or REMATCH, so no
## cross-match signal/state cleanup is needed here.

signal match_ended(winner: Fighter, is_draw: bool)
signal timer_updated(seconds_left: int)
signal round_started()

const STAGE_MIN_X := -560.0
const STAGE_MAX_X := 560.0

@onready var player1: Fighter = $Player1
@onready var player2: Fighter = $Player2
@onready var projectile_layer: Node2D = $ProjectileLayer

var cpu_controller: CPUController
var projectiles: Array[Projectile] = []
var projectile_scene: PackedScene = preload("res://scenes/Projectile.tscn")
var hit_effect_scene: PackedScene = preload("res://scenes/HitEffect.tscn")

var frames_left: int = 0
var match_active: bool = false


func _ready() -> void:
	var p1_stats := DataManager.get_character(GameManager.selected_character_id)
	var p1_moves := DataManager.get_moveset(GameManager.selected_character_id)
	var p2_stats := DataManager.get_character(GameManager.cpu_character_id)
	var p2_moves := DataManager.get_moveset(GameManager.cpu_character_id)
	start_match(p1_stats, p1_moves, p2_stats, p2_moves)


func start_match(p1_stats: CharacterStats, p1_moves: Dictionary, p2_stats: CharacterStats, p2_moves: Dictionary) -> void:
	player1.setup(p1_stats, p1_moves, true, GameManager.p1_key_map())
	player2.setup(p2_stats, p2_moves, false)
	player1.opponent = player2
	player2.opponent = player1
	for p in [player1, player2]:
		p.stage_min_x = STAGE_MIN_X
		p.stage_max_x = STAGE_MAX_X
	player1.global_position = Vector2(-220, 0)
	player2.global_position = Vector2(220, 0)
	player1.facing = Constants.FACING_RIGHT
	player2.facing = Constants.FACING_LEFT

	cpu_controller = CPUController.new(player2, player1)

	player1.ko.connect(_on_ko)
	player2.ko.connect(_on_ko)
	player1.projectile_spawn_requested.connect(_on_projectile_spawn_requested)
	player2.projectile_spawn_requested.connect(_on_projectile_spawn_requested)
	player1.effect_requested.connect(_on_effect_requested)
	player2.effect_requested.connect(_on_effect_requested)
	player1.sound_requested.connect(AudioManager.play_sfx)
	player2.sound_requested.connect(AudioManager.play_sfx)

	frames_left = GameManager.round_time_seconds * Constants.FPS
	match_active = true
	round_started.emit()


func _physics_process(delta: float) -> void:
	if not match_active:
		return

	player2.set_virtual_input(cpu_controller.decide())
	player1.frame_step(delta)
	player2.frame_step(delta)

	_resolve_pushboxes()
	_resolve_combat(player1, player2)
	_resolve_combat(player2, player1)
	_update_projectiles(delta)

	frames_left -= 1
	if frames_left % Constants.FPS == 0:
		timer_updated.emit(int(frames_left / Constants.FPS))
	if frames_left <= 0:
		_end_by_timeout()


func _resolve_pushboxes() -> void:
	var r1 := player1.pushbox.get_rect_world()
	var r2 := player2.pushbox.get_rect_world()
	if not r1.intersects(r2):
		return
	var overlap_x: float = min(r1.position.x + r1.size.x, r2.position.x + r2.size.x) - max(r1.position.x, r2.position.x)
	if overlap_x <= 0:
		return
	var dir: float = signf(player1.global_position.x - player2.global_position.x)
	if dir == 0.0:
		dir = 1.0
	var push := overlap_x / 2.0
	player1.position.x += push * dir
	player2.position.x -= push * dir
	player1.position.x = clamp(player1.position.x, STAGE_MIN_X, STAGE_MAX_X)
	player2.position.x = clamp(player2.position.x, STAGE_MIN_X, STAGE_MAX_X)


func _resolve_combat(attacker: Fighter, defender: Fighter) -> void:
	if not attacker.hitbox.is_active or attacker.current_move_data == null:
		return
	for area in attacker.hitbox.overlapping_hurtboxes():
		var hb := area as Hurtbox
		if hb.owner_fighter != defender or attacker.hitbox.has_hit(defender):
			continue
		attacker.hitbox.mark_hit(defender)
		var move := attacker.current_move_data
		var result := defender.receive_hit(move, attacker)
		attacker.hitstop_timer = max(attacker.hitstop_timer, move.hitstop)
		attacker.gauge.add(move.meter_gain * (0.5 if result.get("blocked", false) else 1.0))


func _update_projectiles(delta: float) -> void:
	var to_remove: Array[Projectile] = []
	for proj in projectiles:
		if not is_instance_valid(proj):
			to_remove.append(proj)
			continue
		var alive := proj.frame_step(delta)
		if alive:
			var target: Fighter = player2 if proj.owner_fighter == player1 else player1
			if proj.hitbox.is_active and not proj.hitbox.has_hit(target) and not target.is_dead:
				for area in proj.hitbox.overlapping_hurtboxes():
					var hb := area as Hurtbox
					if hb.owner_fighter == target:
						proj.hitbox.mark_hit(target)
						target.receive_hit(proj.move, proj.owner_fighter)
						alive = false
						break
		if not alive:
			to_remove.append(proj)
	for proj in to_remove:
		projectiles.erase(proj)
		if is_instance_valid(proj):
			proj.queue_free()


func _on_projectile_spawn_requested(move: MoveData, spawn_pos: Vector2, facing: int, owner_fighter: Fighter) -> void:
	var proj := projectile_scene.instantiate() as Projectile
	projectile_layer.add_child(proj)
	var pdata: Dictionary = move.projectile
	var offset := Vector2(float(pdata.get("spawnOffsetX", 40.0)) * facing, float(pdata.get("spawnOffsetY", -40.0)))
	proj.global_position = spawn_pos + offset
	proj.stage_min_x = STAGE_MIN_X
	proj.stage_max_x = STAGE_MAX_X
	proj.setup(move, owner_fighter, facing)
	projectiles.append(proj)


func _on_effect_requested(kind: String, world_pos: Vector2) -> void:
	var fx := hit_effect_scene.instantiate()
	add_child(fx)
	fx.global_position = world_pos
	if fx.has_method("play"):
		fx.play(kind)


func _on_ko(fighter: Fighter) -> void:
	if not match_active:
		return
	match_active = false
	var winner := player2 if fighter == player1 else player1
	match_ended.emit(winner, false)
	_finish_match(winner, false)


func _end_by_timeout() -> void:
	if not match_active:
		return
	match_active = false
	if player1.current_hp == player2.current_hp:
		match_ended.emit(null, true)
		_finish_match(null, true)
	else:
		var winner := player1 if player1.current_hp > player2.current_hp else player2
		match_ended.emit(winner, false)
		_finish_match(winner, false)


## Brief KO pause (section 4 flow) before handing off to GameManager, which
## owns the actual screen transition (section 38: kept out of MatchController).
func _finish_match(winner: Fighter, is_draw: bool) -> void:
	await get_tree().create_timer(1.2).timeout
	GameManager.report_match_result(winner == player1, is_draw)
