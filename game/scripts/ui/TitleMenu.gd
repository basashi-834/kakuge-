extends Control
## Title screen (section 4). GAME START / CHARACTER EDIT / EXIT, navigable
## with Up/Down + Enter, or mouse click.

@onready var buttons: Array[Button] = [
	$CenterContainer/VBox/StartButton,
	$CenterContainer/VBox/EditButton,
	$CenterContainer/VBox/ExitButton,
]

var selected_index: int = 0

func _ready() -> void:
	for i in range(buttons.size()):
		buttons[i].pressed.connect(_on_button_pressed.bind(i))
		buttons[i].focus_entered.connect(func(): selected_index = i)
	buttons[0].grab_focus()

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		match event.physical_keycode:
			KEY_DOWN:
				_move_selection(1)
			KEY_UP:
				_move_selection(-1)
			KEY_ENTER, KEY_SPACE, KEY_KP_ENTER:
				_on_button_pressed(selected_index)

func _move_selection(delta: int) -> void:
	selected_index = wrapi(selected_index + delta, 0, buttons.size())
	buttons[selected_index].grab_focus()

func _on_button_pressed(index: int) -> void:
	match index:
		0:
			GameManager.goto_game()
		1:
			GameManager.goto_editor()
		2:
			GameManager.quit_game()
