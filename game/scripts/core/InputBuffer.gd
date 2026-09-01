extends RefCounted
class_name InputBuffer
## Keeps a rolling history of the last N frames of input (section 23).
## Each entry stores the held direction as a numpad-notation digit
## (already resolved against the fighter's facing at that instant, so
## CommandParser never has to worry about facing separately) plus any
## buttons newly pressed on that exact frame.
##
## Numpad notation (standard fighting-game convention):
##   7 8 9      forward = the side the fighter faces
##   4 5 6      back    = the opposite side
##   1 2 3

const LENGTH := Constants.INPUT_BUFFER_LENGTH

var _history: Array[Dictionary] = [] # oldest first

## Resolve held direction keys + facing into a single numpad digit.
static func compute_digit(left: bool, right: bool, up: bool, down: bool, facing: int) -> int:
	var forward := right if facing == Constants.FACING_RIGHT else left
	var back := left if facing == Constants.FACING_RIGHT else right
	if down and forward:
		return 3
	if down and back:
		return 1
	if up and forward:
		return 9
	if up and back:
		return 7
	if down:
		return 2
	if up:
		return 8
	if forward:
		return 6
	if back:
		return 4
	return 5


func record_frame(frame_number: int, digit: int, buttons_pressed: Array) -> void:
	_history.append({"frame": frame_number, "digit": digit, "buttons": buttons_pressed.duplicate()})
	while _history.size() > LENGTH:
		_history.pop_front()


func history() -> Array[Dictionary]:
	return _history


func clear() -> void:
	_history.clear()


## True if `button` was pressed on the most recent recorded frame.
func button_just_pressed(button: String) -> bool:
	if _history.is_empty():
		return false
	return _history.back()["buttons"].has(button)
