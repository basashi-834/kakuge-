extends Control
## In-game Character Editor (section 4/31). Builds its form controls in
## code rather than a giant hand-authored .tscn - keeps the scene tiny and
## makes it trivial to add a new editable field later (one call to
## _row(...)). Edits are held in memory until "SAVE", then written via
## DataManager to user:// JSON so they survive a restart.

@onready var char_option: OptionButton = $Root/TopBar/CharacterOption
@onready var move_option: OptionButton = $Root/TopBar/MoveOption
@onready var new_move_button: Button = $Root/TopBar/NewMoveButton
@onready var save_button: Button = $Root/TopBar/SaveButton
@onready var back_button: Button = $Root/TopBar/BackButton
@onready var status_label: Label = $Root/TopBar/StatusLabel

@onready var stats_box: VBoxContainer = $Root/Scroll/Columns/StatsColumn/StatsBox
@onready var move_box: VBoxContainer = $Root/Scroll/Columns/MoveColumn/MoveBox
@onready var advantage_label: Label = $Root/Scroll/Columns/MoveColumn/AdvantageLabel

var current_char_id: String = ""
var current_move_id: String = ""
var current_stats: CharacterStats
var current_moveset: Dictionary = {}

var stat_fields: Dictionary = {}   # field name -> Control
var move_fields: Dictionary = {}   # field name -> Control


func _ready() -> void:
	back_button.pressed.connect(func(): GameManager.goto_title())
	save_button.pressed.connect(_on_save_pressed)
	new_move_button.pressed.connect(_on_new_move_pressed)
	char_option.item_selected.connect(_on_character_selected)
	move_option.item_selected.connect(_on_move_selected)

	_build_stats_form()
	_build_move_form()
	_populate_character_list()


func _populate_character_list() -> void:
	char_option.clear()
	var ids := DataManager.get_character_ids()
	ids.sort()
	for id in ids:
		char_option.add_item(id)
	if ids.size() > 0:
		char_option.select(0)
		_load_character(ids[0])


func _on_character_selected(index: int) -> void:
	_load_character(char_option.get_item_text(index))


func _load_character(char_id: String) -> void:
	current_char_id = char_id
	current_stats = DataManager.get_character(char_id)
	current_moveset = DataManager.get_moveset(char_id)
	_fill_stats_form()
	_populate_move_list()


func _populate_move_list() -> void:
	move_option.clear()
	var ids := current_moveset.keys()
	ids.sort()
	for id in ids:
		move_option.add_item(id)
	if ids.size() > 0:
		move_option.select(0)
		_load_move(ids[0])


func _on_move_selected(index: int) -> void:
	_load_move(move_option.get_item_text(index))


func _load_move(move_id: String) -> void:
	current_move_id = move_id
	_fill_move_form(current_moveset.get(move_id))


# ------------------------------------------------------------------
# Base stats form (section 4 "基本性能")
# ------------------------------------------------------------------
func _build_stats_form() -> void:
	stat_fields["name"] = _row(stats_box, "キャラクター名", _make_line_edit())
	stat_fields["maxHP"] = _row(stats_box, "最大HP", _make_spin(1, 99999, 1))
	stat_fields["walkForwardSpeed"] = _row(stats_box, "前進速度", _make_spin(0, 2000, 1))
	stat_fields["walkBackwardSpeed"] = _row(stats_box, "後退速度", _make_spin(0, 2000, 1))
	stat_fields["dashSpeed"] = _row(stats_box, "ダッシュ速度", _make_spin(0, 3000, 1))
	stat_fields["jumpVelocity"] = _row(stats_box, "ジャンプ力 (負の値)", _make_spin(-3000, 0, 1))
	stat_fields["gravity"] = _row(stats_box, "重力", _make_spin(0, 8000, 1))


func _fill_stats_form() -> void:
	if current_stats == null:
		return
	stat_fields["name"].text = current_stats.name
	stat_fields["maxHP"].value = current_stats.max_hp
	stat_fields["walkForwardSpeed"].value = current_stats.walk_forward_speed
	stat_fields["walkBackwardSpeed"].value = current_stats.walk_backward_speed
	stat_fields["dashSpeed"].value = current_stats.dash_speed
	stat_fields["jumpVelocity"].value = current_stats.jump_velocity
	stat_fields["gravity"].value = current_stats.gravity


func _apply_stats_form() -> void:
	current_stats.name = stat_fields["name"].text
	current_stats.max_hp = int(stat_fields["maxHP"].value)
	current_stats.walk_forward_speed = stat_fields["walkForwardSpeed"].value
	current_stats.walk_backward_speed = stat_fields["walkBackwardSpeed"].value
	current_stats.dash_speed = stat_fields["dashSpeed"].value
	current_stats.jump_velocity = stat_fields["jumpVelocity"].value
	current_stats.gravity = stat_fields["gravity"].value


# ------------------------------------------------------------------
# Move form (section 4 "通常技")
# ------------------------------------------------------------------
func _build_move_form() -> void:
	move_fields["name"] = _row(move_box, "技名", _make_line_edit())
	move_fields["startup"] = _row(move_box, "発生フレーム", _make_spin(1, 999, 1))
	move_fields["active"] = _row(move_box, "持続フレーム", _make_spin(1, 999, 1))
	move_fields["recovery"] = _row(move_box, "硬直フレーム", _make_spin(0, 999, 1))
	move_fields["damage"] = _row(move_box, "ダメージ", _make_spin(0, 9999, 1))
	move_fields["hitstun"] = _row(move_box, "ヒット硬直", _make_spin(0, 999, 1))
	move_fields["blockstun"] = _row(move_box, "ガード硬直", _make_spin(0, 999, 1))
	move_fields["hitstop"] = _row(move_box, "ヒットストップ", _make_spin(0, 60, 1))
	move_fields["knockbackX"] = _row(move_box, "ノックバックX", _make_spin(-3000, 3000, 1))
	move_fields["knockbackY"] = _row(move_box, "ノックバックY", _make_spin(-3000, 3000, 1))
	move_fields["offsetX"] = _row(move_box, "Hitbox offsetX", _make_spin(-500, 500, 1))
	move_fields["offsetY"] = _row(move_box, "Hitbox offsetY", _make_spin(-500, 500, 1))
	move_fields["width"] = _row(move_box, "Hitbox width", _make_spin(1, 500, 1))
	move_fields["height"] = _row(move_box, "Hitbox height", _make_spin(1, 500, 1))
	var guard_option := OptionButton.new()
	for g in [Constants.GUARD_HIGH, Constants.GUARD_LOW, Constants.GUARD_OVERHEAD, Constants.GUARD_THROW]:
		guard_option.add_item(g)
	move_fields["guardType"] = _row(move_box, "ガード属性", guard_option)
	move_fields["cancelable"] = _row(move_box, "キャンセル可否", _make_check())
	move_fields["cancelRoutes"] = _row(move_box, "キャンセル先 (カンマ区切りID)", _make_line_edit())

	for key in move_fields.keys():
		var ctrl: Control = move_fields[key]
		if ctrl is SpinBox:
			ctrl.value_changed.connect(func(_v): _update_advantage_preview())


func _fill_move_form(move: MoveData) -> void:
	if move == null:
		return
	move_fields["name"].text = move.name
	move_fields["startup"].value = move.startup
	move_fields["active"].value = move.active
	move_fields["recovery"].value = move.recovery
	move_fields["damage"].value = move.damage
	move_fields["hitstun"].value = move.hitstun
	move_fields["blockstun"].value = move.blockstun
	move_fields["hitstop"].value = move.hitstop
	move_fields["knockbackX"].value = move.knockback_x
	move_fields["knockbackY"].value = move.knockback_y
	var box: Dictionary = move.hitboxes[0] if not move.hitboxes.is_empty() else {}
	move_fields["offsetX"].value = float(box.get("offsetX", 0.0))
	move_fields["offsetY"].value = float(box.get("offsetY", 0.0))
	move_fields["width"].value = float(box.get("width", 40.0))
	move_fields["height"].value = float(box.get("height", 40.0))
	var guard_opt: OptionButton = move_fields["guardType"]
	var idx := [Constants.GUARD_HIGH, Constants.GUARD_LOW, Constants.GUARD_OVERHEAD, Constants.GUARD_THROW].find(move.guard_type)
	guard_opt.select(max(idx, 0))
	move_fields["cancelable"].button_pressed = move.cancel_end_frame > move.cancel_start_frame
	move_fields["cancelRoutes"].text = ",".join(move.cancel_routes)
	_update_advantage_preview()


func _apply_move_form(move: MoveData) -> void:
	move.name = move_fields["name"].text
	move.startup = int(move_fields["startup"].value)
	move.active = int(move_fields["active"].value)
	move.recovery = int(move_fields["recovery"].value)
	move.total_frame = move.startup + move.active + move.recovery
	move.damage = int(move_fields["damage"].value)
	move.hitstun = int(move_fields["hitstun"].value)
	move.blockstun = int(move_fields["blockstun"].value)
	move.hitstop = int(move_fields["hitstop"].value)
	move.knockback_x = move_fields["knockbackX"].value
	move.knockback_y = move_fields["knockbackY"].value
	move.hitboxes = [{
		"offsetX": move_fields["offsetX"].value, "offsetY": move_fields["offsetY"].value,
		"width": move_fields["width"].value, "height": move_fields["height"].value,
	}]
	var guard_opt: OptionButton = move_fields["guardType"]
	move.guard_type = guard_opt.get_item_text(guard_opt.selected)
	if move_fields["cancelable"].button_pressed:
		move.cancel_start_frame = move.startup
		move.cancel_end_frame = move.startup + move.active + move.recovery
	else:
		move.cancel_start_frame = 0
		move.cancel_end_frame = 0
	var routes_text: String = move_fields["cancelRoutes"].text
	move.cancel_routes = routes_text.split(",", false) if routes_text.strip_edges() != "" else []


func _update_advantage_preview() -> void:
	var startup := int(move_fields["startup"].value)
	var active := int(move_fields["active"].value)
	var recovery := int(move_fields["recovery"].value)
	var hitstun := int(move_fields["hitstun"].value)
	var blockstun := int(move_fields["blockstun"].value)
	var on_hit := hitstun - recovery
	var on_block := blockstun - recovery
	advantage_label.text = "On Hit: %+d   On Block: %+d   TotalFrame: %d" % [on_hit, on_block, startup + active + recovery]


func _on_new_move_pressed() -> void:
	if current_stats == null:
		return
	var base_id := "new_move"
	var new_id := base_id
	var n := 1
	while current_moveset.has(new_id):
		new_id = "%s_%d" % [base_id, n]
		n += 1
	var m := MoveData.new()
	m.id = new_id
	m.name = "New Move"
	m.tags = [Constants.TAG_NORMAL, Constants.TAG_LIGHT]
	m.button = "Light"
	m.stance = "stand"
	current_moveset[new_id] = m
	current_stats.move_ids.append(new_id)
	_populate_move_list()
	for i in range(move_option.item_count):
		if move_option.get_item_text(i) == new_id:
			move_option.select(i)
			_load_move(new_id)
			break
	status_label.text = "新しい技 '%s' を追加しました (SAVEで保存)" % new_id


func _on_save_pressed() -> void:
	if current_stats == null:
		return
	_apply_stats_form()
	DataManager.save_character(current_stats)
	if current_move_id != "" and current_moveset.has(current_move_id):
		var move: MoveData = current_moveset[current_move_id]
		_apply_move_form(move)
		DataManager.save_move(current_char_id, move)
	status_label.text = "保存しました: %s" % Time.get_time_string_from_system()


# ------------------------------------------------------------------
# Small form-building helpers
# ------------------------------------------------------------------
func _row(container: VBoxContainer, label_text: String, control: Control) -> Control:
	var hbox := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size = Vector2(190, 0)
	hbox.add_child(label)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox.add_child(control)
	container.add_child(hbox)
	return control


func _make_spin(min_v: float, max_v: float, step: float) -> SpinBox:
	var s := SpinBox.new()
	s.min_value = min_v
	s.max_value = max_v
	s.step = step
	s.allow_greater = true
	s.allow_lesser = true
	return s


func _make_line_edit() -> LineEdit:
	return LineEdit.new()


func _make_check() -> CheckBox:
	return CheckBox.new()
