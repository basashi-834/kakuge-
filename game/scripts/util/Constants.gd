extends RefCounted
class_name Constants
## Shared enums / string constants used across the whole project.
## Keeping these centralized avoids "magic strings" scattered through
## Character, Move, AI and UI code.

const FPS := 60

# ---- Character state machine -------------------------------------------
enum CharState {
	IDLE,
	WALK_FORWARD,
	WALK_BACKWARD,
	CROUCH,
	JUMP,
	ATTACK,
	BLOCK,
	HITSTUN,
	KNOCKDOWN,
	WAKEUP,
	THROW,
	DEAD,
}

static func state_name(s: CharState) -> String:
	return CharState.keys()[s]

# ---- Guard / hit properties ---------------------------------------------
const GUARD_HIGH := "High"       # blockable standing or crouching
const GUARD_LOW := "Low"         # must crouch-block
const GUARD_OVERHEAD := "Overhead" # must stand-block
const GUARD_THROW := "Throw"     # cannot be blocked normally

# ---- Hit outcome / knockdown severity -----------------------------------
const HIT_NORMAL := "Normal"
const HIT_KNOCKDOWN := "Knockdown"
const HIT_HARD_KNOCKDOWN := "HardKnockdown"
const HIT_LAUNCH := "Launch"
const HIT_WALL_BOUNCE := "WallBounce"
const HIT_GROUND_BOUNCE := "GroundBounce"

# ---- Invincibility --------------------------------------------------------
const INVINCIBLE_NONE := "None"
const INVINCIBLE_FULL := "Full"
const INVINCIBLE_STRIKE := "Strike"
const INVINCIBLE_THROW := "Throw"

# ---- Move tags -------------------------------------------------------------
const TAG_LIGHT := "Light"
const TAG_MEDIUM := "Medium"
const TAG_HEAVY := "Heavy"
const TAG_NORMAL := "Normal"
const TAG_SPECIAL := "Special"
const TAG_SUPER := "Super"
const TAG_ANTI_AIR := "AntiAir"
const TAG_PROJECTILE := "Projectile"
const TAG_LOW := "Low"
const TAG_OVERHEAD := "Overhead"
const TAG_THROW := "Throw"
const TAG_REVERSAL := "Reversal"

# ---- Collision layers (bit index, 1-based as Godot expects) --------------
# Layer usage is intentionally split so Hitbox/Hurtbox/Pushbox never mix.
const LAYER_PUSHBOX := 1
const LAYER_HURTBOX := 2
const LAYER_HITBOX := 3
const LAYER_STAGE := 4

# ---- Facing -----------------------------------------------------------------
const FACING_RIGHT := 1
const FACING_LEFT := -1

# ---- Input buffer -----------------------------------------------------------
const INPUT_BUFFER_LENGTH := 20 # frames of history kept (per spec section 23)
const COMMAND_WINDOW := 16      # leniency window (frames) for motion commands
