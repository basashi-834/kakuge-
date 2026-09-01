extends RefCounted
class_name CharacterStats
## Base performance data for a character (section 5/31).
## Purely data - runtime fields (currentHP, position, velocity, facing)
## live on the Fighter node itself and are re-initialized from this every
## round.

var id: String = ""
var name: String = "Fighter"
var max_hp: int = 1000
var walk_forward_speed: float = 220.0
var walk_backward_speed: float = 170.0
var dash_speed: float = 420.0
var jump_velocity: float = -900.0
var gravity: float = 2400.0
var color: Color = Color(0.8, 0.2, 0.2)
var portrait_color: Color = Color(0.8, 0.2, 0.2)
var move_ids: Array = [] # ordered list of move ids this character owns

static func from_dict(d: Dictionary) -> CharacterStats:
	var s := CharacterStats.new()
	s.id = d.get("id", "")
	s.name = d.get("name", s.id)
	s.max_hp = int(d.get("maxHP", 1000))
	s.walk_forward_speed = float(d.get("walkForwardSpeed", 220.0))
	s.walk_backward_speed = float(d.get("walkBackwardSpeed", 170.0))
	s.dash_speed = float(d.get("dashSpeed", 420.0))
	s.jump_velocity = float(d.get("jumpVelocity", -900.0))
	s.gravity = float(d.get("gravity", 2400.0))
	var c: Array = d.get("color", [0.8, 0.2, 0.2])
	if c.size() >= 3:
		s.color = Color(c[0], c[1], c[2])
	s.move_ids = d.get("moves", [])
	return s

func to_dict() -> Dictionary:
	return {
		"id": id,
		"name": name,
		"maxHP": max_hp,
		"walkForwardSpeed": walk_forward_speed,
		"walkBackwardSpeed": walk_backward_speed,
		"dashSpeed": dash_speed,
		"jumpVelocity": jump_velocity,
		"gravity": gravity,
		"color": [color.r, color.g, color.b],
		"moves": move_ids,
	}
