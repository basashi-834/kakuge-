extends Node2D
## Placeholder "sprite" (section 32). No art assets yet, so every state is
## drawn as simple procedural shapes with a distinct color/pose - this is
## intentionally isolated from Fighter's gameplay logic (it only READS
## Fighter's public state) so real spritesheets can replace it later
## without touching combat code.

var fighter: Fighter

const BODY_W := 46.0
const BODY_H := 100.0
const CROUCH_H := 62.0

const TAG_COLORS := {
	"Light": Color(0.95, 0.85, 0.2),
	"Medium": Color(0.95, 0.55, 0.15),
	"Heavy": Color(0.9, 0.2, 0.15),
	"Special": Color(0.6, 0.25, 0.95),
	"Super": Color(1.0, 0.15, 0.55),
}

func _ready() -> void:
	fighter = get_parent() as Fighter
	set_process(true)

func _process(_delta: float) -> void:
	queue_redraw()

func _draw() -> void:
	if fighter == null:
		return
	var state: int = fighter.state_machine.current_state
	var base_color: Color = fighter.stats.color if fighter.stats else Color.WHITE
	var facing: int = fighter.facing

	match state:
		Constants.CharState.KNOCKDOWN:
			draw_rect(Rect2(Vector2(-BODY_H * 0.5, -22), Vector2(BODY_H, 22)), base_color.darkened(0.3))
		Constants.CharState.WAKEUP:
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -CROUCH_H * 0.7), Vector2(BODY_W, CROUCH_H * 0.7)), base_color.darkened(0.15))
		Constants.CharState.CROUCH:
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -CROUCH_H), Vector2(BODY_W, CROUCH_H)), base_color)
		Constants.CharState.BLOCK:
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -BODY_H), Vector2(BODY_W, BODY_H)), base_color.lerp(Color.CYAN, 0.35))
			draw_rect(Rect2(Vector2(facing * 18 - 6, -BODY_H * 0.6), Vector2(20, 40)), Color(0.3, 0.6, 1.0))
		Constants.CharState.HITSTUN:
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -BODY_H), Vector2(BODY_W, BODY_H)), base_color.lerp(Color.WHITE, 0.5))
		Constants.CharState.THROW:
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -BODY_H * 0.8), Vector2(BODY_W, BODY_H * 0.8)), base_color.lerp(Color.RED, 0.3))
		Constants.CharState.DEAD:
			draw_rect(Rect2(Vector2(-BODY_H * 0.5, -14), Vector2(BODY_H, 14)), base_color.darkened(0.5))
		Constants.CharState.ATTACK:
			var h := BODY_H
			var move := fighter.current_move_data
			var color := base_color
			if move != null:
				for tag in ["Super", "Special", "Heavy", "Medium", "Light"]:
					if move.has_tag(tag):
						color = TAG_COLORS[tag]
						break
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -h), Vector2(BODY_W, h)), color)
			# limb thrust in facing direction while a hitbox would be live
			draw_rect(Rect2(Vector2(facing * 8, -h * 0.65), Vector2(facing * 46, 16)), color.lightened(0.3))
		Constants.CharState.JUMP:
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -BODY_H), Vector2(BODY_W, BODY_H)), base_color.lightened(0.1))
		Constants.CharState.WALK_FORWARD, Constants.CharState.WALK_BACKWARD:
			var bob := sin(Time.get_ticks_msec() * 0.02) * 4.0
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -BODY_H + bob), Vector2(BODY_W, BODY_H)), base_color)
		_: # IDLE
			draw_rect(Rect2(Vector2(-BODY_W * 0.5, -BODY_H), Vector2(BODY_W, BODY_H)), base_color)

	# facing indicator (small triangle "nose")
	var nose_x := facing * (BODY_W * 0.5 + 6)
	draw_circle(Vector2(nose_x, -BODY_H * 0.85), 5, base_color.lightened(0.4))
