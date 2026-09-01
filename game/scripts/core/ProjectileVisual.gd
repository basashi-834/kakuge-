extends Node2D
## Placeholder projectile sprite - a simple spinning circle so it reads
## clearly against the fighters (section 10 "アニメーション素材がない場合は
## 長方形・円...で構いません").

func _process(_delta: float) -> void:
	rotation += 0.35
	queue_redraw()

func _draw() -> void:
	draw_circle(Vector2.ZERO, 16.0, Color(0.9, 0.6, 0.15))
	draw_arc(Vector2.ZERO, 16.0, 0, TAU, 16, Color(1.0, 1.0, 0.6), 3.0)
	draw_line(Vector2(-16, 0), Vector2(16, 0), Color(1.0, 1.0, 0.6), 2.0)
	draw_line(Vector2(0, -16), Vector2(0, 16), Color(1.0, 1.0, 0.6), 2.0)
