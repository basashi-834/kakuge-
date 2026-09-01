extends Area2D
class_name Pushbox
## Character-vs-character solid body (section 7 / 36). Kept entirely
## separate from Hitbox/Hurtbox so attack range and body collision never
## interfere with each other. Resolution (keeping two pushboxes from
## overlapping) is done centrally by CombatResolver each physics frame so
## both fighters are corrected symmetrically and deterministically.

@onready var shape: CollisionShape2D = $CollisionShape2D

var owner_fighter: Node = null
var half_size: Vector2 = Vector2(28, 55)

func _ready() -> void:
	collision_layer = 1 << (Constants.LAYER_PUSHBOX - 1)
	collision_mask = 1 << (Constants.LAYER_PUSHBOX - 1)
	monitoring = true
	monitorable = true
	if shape.shape == null:
		shape.shape = RectangleShape2D.new()
	(shape.shape as RectangleShape2D).size = half_size * 2.0

func get_rect_world() -> Rect2:
	var c: Vector2 = global_position
	return Rect2(c - half_size, half_size * 2.0)
