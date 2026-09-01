extends Node2D
class_name Projectile
## Standalone object (section 26), intentionally NOT a Fighter - it only
## needs a Hitbox (reused as-is from the character system) plus simple
## linear movement and a lifetime counter.

@onready var hitbox: Hitbox = $Hitbox
@onready var visual: Node2D = $Visual

var move: MoveData
var owner_fighter: Fighter
var speed: float = 500.0
var lifetime_frames: int = 90
var facing: int = 1
var stage_min_x: float = -600.0
var stage_max_x: float = 600.0

signal expired(projectile: Projectile)

func setup(p_move: MoveData, p_owner: Fighter, p_facing: int) -> void:
	move = p_move
	owner_fighter = p_owner
	facing = p_facing
	var pdata: Dictionary = move.projectile
	speed = float(pdata.get("speed", 500.0))
	lifetime_frames = int(pdata.get("lifetime", 90))
	var w := float(pdata.get("width", 30.0))
	var h := float(pdata.get("height", 30.0))
	hitbox.owner_fighter = p_owner
	hitbox.activate(Vector2.ZERO, Vector2(w, h))


func frame_step(delta: float) -> bool:
	position.x += speed * facing * delta
	lifetime_frames -= 1
	if lifetime_frames <= 0 or position.x < stage_min_x - 100 or position.x > stage_max_x + 100:
		expired.emit(self)
		return false
	return true
