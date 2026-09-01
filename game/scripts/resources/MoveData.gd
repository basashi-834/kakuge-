extends RefCounted
class_name MoveData
## Data-only description of a single move (normal, special or super).
## Loaded from JSON under res://moves/<character_id>/*.json (or a user://
## override written by the Character Editor). Never subclassed per move -
## behaviour lives in MoveExecutor, this is pure data (see design doc
## section 8/38: "技をコードに直接大量記述しない").

var id: String = ""
var name: String = ""

# Frame data (section 11)
var startup: int = 1
var active: int = 1
var recovery: int = 1
var total_frame: int = 0 # auto-computed if 0 in JSON

# Combat numbers
var damage: int = 0
var hitstun: int = 0
var blockstun: int = 0
var hitstop: int = 0
var guard_type: String = Constants.GUARD_HIGH
var chip_damage_percent: float = 0.0 # section 15: chip damage, off by default

# Hitbox: list so a move can have more than one active box in future.
# Each entry: {"offset_x":x, "offset_y":y, "width":w, "height":h}
var hitboxes: Array = []

# Knockback
var knockback_x: float = 0.0
var knockback_y: float = 0.0
var hit_outcome: String = Constants.HIT_NORMAL # Normal / Knockdown / HardKnockdown / Launch / ...

# Meter
var meter_gain: int = 0
var meter_cost: int = 0

# Cancel routes / window (section 21/22)
var cancel_routes: Array = []
var cancel_start_frame: int = 0
var cancel_end_frame: int = 0

# Tags (section 9)
var tags: Array = []

# Invincibility (section 20): {"type":"Strike","start_frame":1,"end_frame":6}
var invincibility: Dictionary = {"type": Constants.INVINCIBLE_NONE, "start_frame": 0, "end_frame": 0}

# Input requirement
var input_command: String = ""  # e.g. "236" for a quarter-circle-forward move, "" for a plain normal
var button: String = ""         # "Light" / "Medium" / "Heavy" / "Special" / "Super"
var stance: String = "any"      # "stand" / "crouch" / "air" / "any" - required stance to use this move
var requires_air: bool = false

# Projectile sub-data (section 26), only used when tags contains "Projectile"
var projectile: Dictionary = {}

# CPU decision-making hint (section 28): the distance (px) this move is
# intended to be used at. Falls back to a tag-based heuristic if the JSON
# omits it, so hand-authored data doesn't have to specify everything.
var effective_range: float = 0.0


static func from_dict(d: Dictionary) -> MoveData:
	var m := MoveData.new()
	m.id = d.get("id", "")
	m.name = d.get("name", m.id)
	m.startup = int(d.get("startup", 1))
	m.active = int(d.get("active", 1))
	m.recovery = int(d.get("recovery", 1))
	var tf: int = int(d.get("totalFrame", 0))
	m.total_frame = tf if tf > 0 else (m.startup + m.active + m.recovery)
	m.damage = int(d.get("damage", 0))
	m.hitstun = int(d.get("hitstun", 0))
	m.blockstun = int(d.get("blockstun", 0))
	m.hitstop = int(d.get("hitstop", 0))
	m.guard_type = d.get("guardType", Constants.GUARD_HIGH)
	m.chip_damage_percent = float(d.get("chipDamagePercent", 0.0))
	m.hitboxes = d.get("hitbox", [])
	m.knockback_x = float(d.get("knockbackX", 0.0))
	m.knockback_y = float(d.get("knockbackY", 0.0))
	m.hit_outcome = d.get("hitOutcome", Constants.HIT_NORMAL)
	m.meter_gain = int(d.get("meterGain", 0))
	m.meter_cost = int(d.get("meterCost", 0))
	m.cancel_routes = d.get("cancelRoutes", [])
	m.cancel_start_frame = int(d.get("cancelStartFrame", 0))
	m.cancel_end_frame = int(d.get("cancelEndFrame", 0))
	m.tags = d.get("tags", [])
	m.invincibility = d.get("invincibility", {"type": Constants.INVINCIBLE_NONE, "start_frame": 0, "end_frame": 0})
	m.input_command = d.get("input", "")
	m.button = d.get("button", "")
	m.stance = d.get("stance", "any")
	m.requires_air = bool(d.get("requiresAir", false))
	m.projectile = d.get("projectile", {})
	m.effective_range = float(d.get("effectiveRange", 0.0))
	if m.effective_range <= 0.0:
		if m.tags.has(Constants.TAG_PROJECTILE):
			m.effective_range = 900.0
		elif m.tags.has(Constants.TAG_THROW):
			m.effective_range = 55.0
		elif m.tags.has(Constants.TAG_HEAVY):
			m.effective_range = 100.0
		elif m.tags.has(Constants.TAG_MEDIUM):
			m.effective_range = 85.0
		else:
			m.effective_range = 70.0
	return m


func to_dict() -> Dictionary:
	return {
		"id": id,
		"name": name,
		"startup": startup,
		"active": active,
		"recovery": recovery,
		"totalFrame": total_frame,
		"damage": damage,
		"hitstun": hitstun,
		"blockstun": blockstun,
		"hitstop": hitstop,
		"guardType": guard_type,
		"chipDamagePercent": chip_damage_percent,
		"hitbox": hitboxes,
		"knockbackX": knockback_x,
		"knockbackY": knockback_y,
		"hitOutcome": hit_outcome,
		"meterGain": meter_gain,
		"meterCost": meter_cost,
		"cancelRoutes": cancel_routes,
		"cancelStartFrame": cancel_start_frame,
		"cancelEndFrame": cancel_end_frame,
		"tags": tags,
		"invincibility": invincibility,
		"input": input_command,
		"button": button,
		"stance": stance,
		"requiresAir": requires_air,
		"projectile": projectile,
		"effectiveRange": effective_range,
	}


func has_tag(tag: String) -> bool:
	return tags.has(tag)


func can_cancel_into(move_id: String) -> bool:
	return cancel_routes.has(move_id)


func is_cancel_window_open(frame: int) -> bool:
	if cancel_start_frame <= 0 and cancel_end_frame <= 0:
		return false
	return frame >= cancel_start_frame and frame <= cancel_end_frame


## On-hit / on-block frame advantage, used by debug UI + editor (section 16).
## Positive = attacker recovers first (advantage). Formula: recovery frames
## remaining for defender (stun) minus recovery frames remaining for attacker.
func on_hit_advantage() -> int:
	var attacker_recovery_left := recovery
	return hitstun - attacker_recovery_left


func on_block_advantage() -> int:
	var attacker_recovery_left := recovery
	return blockstun - attacker_recovery_left
