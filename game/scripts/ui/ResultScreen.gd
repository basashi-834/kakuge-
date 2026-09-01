extends Control
## Result screen (section 4). Shows PLAYER WIN / CPU WIN / DRAW, with
## REMATCH (same conditions) and TITLE buttons.

@onready var result_label: Label = $VBox/ResultLabel
@onready var rematch_button: Button = $VBox/RematchButton
@onready var title_button: Button = $VBox/TitleButton

func _ready() -> void:
	if GameManager.last_is_draw:
		result_label.text = "DRAW"
	elif GameManager.last_winner_is_player:
		result_label.text = "PLAYER WIN"
	else:
		result_label.text = "CPU WIN"
	rematch_button.pressed.connect(func(): GameManager.rematch())
	title_button.pressed.connect(func(): GameManager.goto_title())
	rematch_button.grab_focus()

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.physical_keycode in [KEY_LEFT, KEY_RIGHT, KEY_TAB]:
			if rematch_button.has_focus():
				title_button.grab_focus()
			else:
				rematch_button.grab_focus()
