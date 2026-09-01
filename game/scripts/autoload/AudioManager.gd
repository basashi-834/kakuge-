extends Node
## Autoload singleton. Centralizes sound playback so Fighter/MoveExecutor
## never touch AudioStreamPlayer directly (section 34). No audio assets are
## bundled yet - this silently no-ops when a clip is missing so gameplay
## code never has to know or care whether sound exists. Drop .ogg/.wav
## files into res://audio/ using the keys below and they start playing
## automatically, no code changes required.

const SFX_PATHS := {
	"attack": "res://audio/attack.ogg",
	"hit": "res://audio/hit.ogg",
	"block": "res://audio/block.ogg",
	"ko": "res://audio/ko.ogg",
}

var _players: Array[AudioStreamPlayer] = []
var _streams: Dictionary = {}
const POOL_SIZE := 8

func _ready() -> void:
	for key in SFX_PATHS.keys():
		var path: String = SFX_PATHS[key]
		if ResourceLoader.exists(path):
			_streams[key] = load(path)
	for i in range(POOL_SIZE):
		var p := AudioStreamPlayer.new()
		add_child(p)
		_players.append(p)


func play_sfx(kind: String) -> void:
	if not _streams.has(kind):
		return # no clip authored yet - safe no-op (section 34)
	for p in _players:
		if not p.playing:
			p.stream = _streams[kind]
			p.play()
			return
