extends Node2D
## Placeholder VFX (section 33). One generic scene, visually differentiated
## by `kind` so normal hit / heavy hit / guard / special / super all read
## distinctly even with simple circle/rect graphics - real sprites can
## replace draw() later without touching gameplay code.

var _kind: String = "hit"
var _timer: float = 0.0
var _duration: float = 0.16
var _color: Color = Color.WHITE
var _radius: float = 18.0

const STYLES := {
	"hit": {"color": Color(1, 1, 0.3), "radius": 16.0, "duration": 0.14},
	"heavy_hit": {"color": Color(1, 0.5, 0.1), "radius": 26.0, "duration": 0.18},
	"guard": {"color": Color(0.4, 0.7, 1.0), "radius": 20.0, "duration": 0.14},
	"special": {"color": Color(0.6, 0.2, 1.0), "radius": 30.0, "duration": 0.22},
	"super": {"color": Color(1.0, 0.2, 0.2), "radius": 46.0, "duration": 0.32},
}

func play(kind: String) -> void:
	_kind = kind
	var style: Dictionary = STYLES.get(kind, STYLES["hit"])
	_color = style["color"]
	_radius = style["radius"]
	_duration = style["duration"]
	_timer = _duration
	queue_redraw()

func _process(delta: float) -> void:
	_timer -= delta
	queue_redraw()
	if _timer <= 0.0:
		queue_free()

func _draw() -> void:
	if _timer <= 0.0:
		return
	var t: float = 1.0 - clampf(_timer / _duration, 0.0, 1.0)
	var r := _radius * (0.4 + t * 1.1)
	var a := 1.0 - t
	draw_circle(Vector2.ZERO, r, Color(_color.r, _color.g, _color.b, a * 0.85))
	draw_arc(Vector2.ZERO, r * 1.3, 0, TAU, 24, Color(1, 1, 1, a), 3.0)
