extends Area2D
class_name Hitbox
## Attack collision (section 7). Position/size changes per move/per frame,
## driven entirely by MoveExecutor - this node has no attack knowledge of
## its own besides "am I currently active".

@onready var shape: CollisionShape2D = $CollisionShape2D

var owner_fighter: Node = null
var is_active: bool = false
var already_hit: Array = [] # fighters already hit during this activation (section 12)

func _ready() -> void:
	collision_layer = 1 << (Constants.LAYER_HITBOX - 1)
	collision_mask = 1 << (Constants.LAYER_HURTBOX - 1)
	monitoring = false
	monitorable = false
	if shape.shape == null:
		shape.shape = RectangleShape2D.new()

func activate(offset: Vector2, size: Vector2) -> void:
	is_active = true
	already_hit.clear()
	monitoring = true
	(shape.shape as RectangleShape2D).size = size
	shape.position = offset
	shape.disabled = false

func deactivate() -> void:
	is_active = false
	monitoring = false
	shape.disabled = true

func has_hit(target: Node) -> bool:
	return already_hit.has(target)

func mark_hit(target: Node) -> void:
	already_hit.append(target)

## Overlapping opposing hurtboxes right now (polled explicitly by
## CombatResolver for deterministic, frame-exact resolution rather than
## relying on async area_entered signals).
func overlapping_hurtboxes() -> Array:
	var result: Array = []
	for a in get_overlapping_areas():
		if a is Hurtbox:
			result.append(a)
	return result
