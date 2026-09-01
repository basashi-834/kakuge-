extends RefCounted
class_name SuperGauge
## Super meter (section 25). 0-100, gains on dealing AND receiving hits,
## supers are locked out below their meter_cost.

const MAX_VALUE := 100

var value: float = 0.0

func add(amount: float) -> void:
	value = clampf(value + amount, 0.0, MAX_VALUE)

func can_spend(cost: float) -> bool:
	return value >= cost

func spend(cost: float) -> bool:
	if not can_spend(cost):
		return false
	value = clampf(value - cost, 0.0, MAX_VALUE)
	return true

func percent() -> float:
	return value / MAX_VALUE
