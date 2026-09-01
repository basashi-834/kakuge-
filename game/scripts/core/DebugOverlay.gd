extends Node2D
## Debug visualization (section 30). Toggled with F1. Draws Hitbox (red),
## Hurtbox (blue), Pushbox (green) so their differences are obvious, plus a
## text readout of state/move/frame/HP/gauge/velocity/hitstun/blockstun for
## both fighters.

var match_controller: MatchController

func _ready() -> void:
	match_controller = get_parent() as MatchController
	set_process(true)
	top_level = false
	z_index = 100

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_F1:
		GameManager.toggle_debug()

func _process(_delta: float) -> void:
	visible = GameManager.debug_visible
	if visible:
		queue_redraw()

func _draw() -> void:
	if match_controller == null:
		return
	for fighter in [match_controller.player1, match_controller.player2]:
		_draw_pushbox(fighter)
		_draw_hurtbox(fighter)
		_draw_hitbox(fighter)
		_draw_text(fighter)

func _rect_from_shape(cs: CollisionShape2D) -> Rect2:
	if cs.disabled or cs.shape == null:
		return Rect2()
	var size: Vector2 = (cs.shape as RectangleShape2D).size
	return Rect2(cs.global_position - size / 2.0, size)

func _draw_pushbox(f: Fighter) -> void:
	var r := f.pushbox.get_rect_world()
	draw_rect(r, Color(0.2, 1.0, 0.2), false, 2.0)

func _draw_hurtbox(f: Fighter) -> void:
	for shape_node in [f.hurtbox.stand_shape, f.hurtbox.crouch_shape, f.hurtbox.air_shape]:
		var r := _rect_from_shape(shape_node)
		if r.size != Vector2.ZERO:
			draw_rect(r, Color(0.25, 0.5, 1.0), false, 2.0)

func _draw_hitbox(f: Fighter) -> void:
	if f.hitbox.is_active:
		var r := _rect_from_shape(f.hitbox.shape)
		if r.size != Vector2.ZERO:
			draw_rect(r, Color(1.0, 0.15, 0.15), false, 3.0)

func _draw_text(f: Fighter) -> void:
	var info := f.debug_info()
	var lines := [
		"state=%s move=%s frame=%d" % [info.state, info.move, info.frame],
		"hp=%d gauge=%.0f" % [info.hp, info.gauge],
		"vel=(%.0f,%.0f)" % [info.velocity.x, info.velocity.y],
		"hitstun=%d blockstun=%d hitstop=%d" % [info.hitstun, info.blockstun, info.hitstop],
	]
	var pos := f.global_position + Vector2(-70, -170)
	var font := ThemeDB.fallback_font
	for i in range(lines.size()):
		draw_string(font, pos + Vector2(0, i * 14), lines[i], HORIZONTAL_ALIGNMENT_LEFT, 220, 12, Color.WHITE)
