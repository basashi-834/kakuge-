# UI/AudioHelper.ps1
# Sound playback (section 34). WinForms has no game audio mixer, so this
# uses System.Media.SoundPlayer, which only supports uncompressed .wav
# files (a real format limitation vs. the earlier Godot prototype, called
# out in README.md). Silently no-ops when a clip is missing so gameplay
# never depends on audio existing - drop matching .wav files into Audio\
# and they play automatically, no code changes required.

$script:SoundPlayers = @{}
$script:AudioDir = $null

function Initialize-Audio([string]$audioDir) {
    $script:AudioDir = $audioDir
    $script:SoundPlayers = @{}
    foreach ($kind in @("attack", "hit", "block", "ko")) {
        $path = Join-Path $audioDir "$kind.wav"
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            try {
                $player = New-Object System.Media.SoundPlayer($path)
                $player.Load()
                $script:SoundPlayers[$kind] = $player
            } catch { }
        }
    }
}

function Play-Sound([string]$kind) {
    if ($script:SoundPlayers.ContainsKey($kind)) {
        try { $script:SoundPlayers[$kind].Play() } catch { }
    }
}
