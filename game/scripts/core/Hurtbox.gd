extends Area2D
class_name Hurtbox
## Damageable area (section 7). Shape changes with stance (Stand/Crouch/
## Air per section 7 minimum requirement) by toggling which child
## CollisionShape2D is enabled - each stance keeps its own hand-authored
## rectangle so e.g. crouching correctly shrinks the box.

@onready var stand_shape: CollisionShape2D = $Stand
@onready var crouch_shape: CollisionShape2D = $Crouch
@onready var air_shape: CollisionShape2D = $Air

var owner_fighter: Node = null

func _ready() -> void:
	collision_layer = 1 << (Constants.LAYER_HURTBOX - 1)
	collision_mask = 0
	monitoring = false
	monitorable = true
	set_stance("stand")

func set_stance(stance: String) -> void:
	stand_shape.disabled = stance != "stand"
	crouch_shape.disabled = stance != "crouch"
	air_shape.disabled = stance != "air"
