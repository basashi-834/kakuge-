extends RefCounted
class_name MoveExecutor
## Frame management for whichever move is currently playing (section 11).
## Deliberately stateless (phase is derived from move + current frame) so
## it can never desync from StateMachine.current_frame, which is the single
## source of truth for "how many frames have we been in this move".
##
##   Startup:  frame in [1, startup-1]
##   Active:   frame in [startup, startup+active-1]
##   Recovery: frame in [startup+active, startup+active+recovery-1]
##   Done:     frame >= startup+active+recovery
##
## Example from the design doc (startup 5 / active 3 / recovery 12):
##   1-4 startup, 5-7 active, 8-19 recovery, 20+ done.

const PHASE_STARTUP := "startup"
const PHASE_ACTIVE := "active"
const PHASE_RECOVERY := "recovery"
const PHASE_DONE := "done"

static func get_phase(move: MoveData, frame: int) -> String:
	if frame < move.startup:
		return PHASE_STARTUP
	elif frame < move.startup + move.active:
		return PHASE_ACTIVE
	elif frame < move.startup + move.active + move.recovery:
		return PHASE_RECOVERY
	return PHASE_DONE

static func is_invincible(move: MoveData, frame: int, kind: String = "") -> bool:
	var inv: Dictionary = move.invincibility
	var itype: String = inv.get("type", Constants.INVINCIBLE_NONE)
	if itype == Constants.INVINCIBLE_NONE:
		return false
	var start: int = int(inv.get("start_frame", 0))
	var end: int = int(inv.get("end_frame", 0))
	if frame < start or frame > end:
		return false
	if kind == "":
		return true
	if itype == Constants.INVINCIBLE_FULL:
		return true
	return itype == kind

static func can_cancel(move: MoveData, frame: int) -> bool:
	return move.is_cancel_window_open(frame)

## Drives a single Hitbox node's activation state based on the move's
## current phase. `hitbox` is a Hitbox node; `facing` flips the x offset so
## authored hitbox data is always relative to "facing right".
static func drive_hitbox(hitbox: Hitbox, move: MoveData, frame: int, facing: int) -> void:
	var phase := get_phase(move, frame)
	if phase == PHASE_ACTIVE and not move.hitboxes.is_empty():
		if not hitbox.is_active:
			var box: Dictionary = move.hitboxes[0]
			var offset := Vector2(float(box.get("offsetX", 0.0)) * facing, float(box.get("offsetY", 0.0)))
			var size := Vector2(float(box.get("width", 40.0)), float(box.get("height", 40.0)))
			hitbox.activate(offset, size)
	else:
		if hitbox.is_active:
			hitbox.deactivate()
