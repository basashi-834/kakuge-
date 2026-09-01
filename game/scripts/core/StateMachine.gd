extends RefCounted
class_name StateMachine
## Pure state-tracking for a Fighter (section 6). Deliberately dumb: it only
## knows the CURRENT state + how many frames it has been in it, plus the id
## of the move currently playing (if any). All the "what should happen"
## logic lives in Fighter / MoveExecutor - this class just remembers where
## we are, which is what the debug overlay and editor need to display
## (currentState / currentMove / currentFrame - section 6).

var current_state: Constants.CharState = Constants.CharState.IDLE
var previous_state: Constants.CharState = Constants.CharState.IDLE
var current_frame: int = 0       # frames spent in current_state
var current_move: String = ""    # move id, "" when not attacking

signal state_changed(old_state: Constants.CharState, new_state: Constants.CharState)

func change_state(new_state: Constants.CharState, move_id: String = "") -> void:
	if new_state == current_state and move_id == current_move:
		return
	previous_state = current_state
	current_state = new_state
	current_move = move_id
	current_frame = 0
	state_changed.emit(previous_state, current_state)

func tick() -> void:
	current_frame += 1

func is_in(states: Array) -> bool:
	return states.has(current_state)

func is_attacking() -> bool:
	return current_state == Constants.CharState.ATTACK

func is_actionable() -> bool:
	## Can the player/AI start a NEW voluntary action (move/jump/attack)?
	return current_state in [Constants.CharState.IDLE, Constants.CharState.WALK_FORWARD,
		Constants.CharState.WALK_BACKWARD, Constants.CharState.CROUCH]
