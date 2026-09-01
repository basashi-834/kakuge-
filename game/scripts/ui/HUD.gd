extends CanvasLayer
class_name HUDLayer
## HUD is purely presentational - it only reads Fighter/MatchController
## public state via signals, never touches combat logic (section 38:
## "UIと戦闘処理を分離する").

@onready var p1_hp_bar: ProgressBar = $Control/P1HPBar
@onready var p2_hp_bar: ProgressBar = $Control/P2HPBar
@onready var p1_gauge_bar: ProgressBar = $Control/P1GaugeBar
@onready var p2_gauge_bar: ProgressBar = $Control/P2GaugeBar
@onready var timer_label: Label = $Control/TimerLabel
@onready var round_label: Label = $Control/RoundLabel
@onready var p1_name_label: Label = $Control/P1NameLabel
@onready var p2_name_label: Label = $Control/P2NameLabel

var match_controller: MatchController

func _ready() -> void:
	match_controller = get_parent() as MatchController
	match_controller.round_started.connect(_on_round_started)
	match_controller.timer_updated.connect(_on_timer_updated)
	round_label.text = "ROUND %d" % 1
	timer_label.text = "99"


func _on_round_started() -> void:
	var p1 := match_controller.player1
	var p2 := match_controller.player2
	p1_hp_bar.max_value = p1.stats.max_hp
	p2_hp_bar.max_value = p2.stats.max_hp
	p1_name_label.text = p1.stats.name
	p2_name_label.text = p2.stats.name
	p1.hp_changed.connect(_on_p1_hp)
	p2.hp_changed.connect(_on_p2_hp)
	_on_p1_hp(p1.current_hp, p1.stats.max_hp)
	_on_p2_hp(p2.current_hp, p2.stats.max_hp)


func _on_p1_hp(cur: int, _max_hp: int) -> void:
	p1_hp_bar.value = cur


func _on_p2_hp(cur: int, _max_hp: int) -> void:
	p2_hp_bar.value = cur


func _on_timer_updated(seconds_left: int) -> void:
	timer_label.text = str(seconds_left)


func _process(_delta: float) -> void:
	if match_controller and match_controller.player1 and match_controller.player2:
		p1_gauge_bar.value = match_controller.player1.gauge.value
		p2_gauge_bar.value = match_controller.player2.gauge.value
