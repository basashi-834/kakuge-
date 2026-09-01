extends RefCounted
class_name CommandParser
## Recognizes special-move motion commands out of an InputBuffer's history.
## Matching is a bounded, in-order SUBSEQUENCE search rather than an exact
## frame-by-frame match - this is what gives fighting games their
## "generous" feel (section 24: "完全一致ではなく...多少余裕を持たせる").

## Known motions, expressed as required numpad-digit sequences.
const MOTIONS := {
	"236": [2, 3, 6],        # quarter-circle forward
	"214": [2, 1, 4],        # quarter-circle back
	"623": [6, 2, 3],        # dragon-punch / anti-air motion
	"236236": [2, 3, 6, 2, 3, 6], # double quarter-circle forward (super)
}

## Returns true if `input` (e.g. "236") followed by `button` was performed
## within the trailing `window` frames of `buffer`'s history.
static func matches(buffer: InputBuffer, input_command: String, button: String, window: int = Constants.COMMAND_WINDOW) -> bool:
	if input_command == "":
		return false
	var digits: Array = MOTIONS.get(input_command, [])
	if digits.is_empty():
		return false
	var history := buffer.history()
	if history.is_empty():
		return false
	var last_frame: int = history.back()["frame"]
	var relevant: Array[Dictionary] = []
	for entry in history:
		if last_frame - entry["frame"] <= window:
			relevant.append(entry)

	# 1) subsequence match the direction digits, in order.
	var ptr := 0
	var match_frame := -1
	for entry in relevant:
		if ptr < digits.size() and entry["digit"] == digits[ptr]:
			ptr += 1
			match_frame = entry["frame"]
			if ptr >= digits.size():
				break
	if ptr < digits.size():
		return false

	# 2) the button must be pressed at/after the motion completed, within a
	#    short trailing grace window (a few frames), matching how players
	#    naturally finish a motion then tap the button.
	var button_grace := 8
	for entry in relevant:
		if entry["frame"] >= match_frame and entry["frame"] - match_frame <= button_grace:
			if entry["buttons"].has(button):
				return true
	return false
