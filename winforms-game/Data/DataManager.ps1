# Data/DataManager.ps1
# Owns ALL external data I/O: character base stats + move frame data.
#
# Default data ships read-only next to the launcher under Data/characters
# and Data/moves/<id>/. The Character Editor writes edits to a per-user
# folder (%APPDATA%\Kakuge\ on Windows) so changes survive a restart
# without ever touching the shipped files (section 31/4: "編集内容は保存
# できるようにしてください"). Resolution order per file: user override >
# shipped default - same rule as the earlier Godot prototype's res:// vs
# user:// split.

class DataManager {
    [string]$BaseDir
    [string]$UserDir
    [hashtable]$Characters = @{}   # id -> CharacterStats
    [hashtable]$Movesets = @{}     # charId -> hashtable(moveId -> MoveData)

    DataManager([string]$baseDir, [string]$userDir) {
        $this.BaseDir = $baseDir
        $this.UserDir = $userDir
        if (-not (Test-Path -LiteralPath (Join-Path $this.UserDir "characters"))) {
            New-Item -ItemType Directory -Path (Join-Path $this.UserDir "characters") -Force | Out-Null
        }
        if (-not (Test-Path -LiteralPath (Join-Path $this.UserDir "moves"))) {
            New-Item -ItemType Directory -Path (Join-Path $this.UserDir "moves") -Force | Out-Null
        }
    }

    [void] ReloadAll() {
        $this.Characters = @{}
        $this.Movesets = @{}
        $this.LoadCharactersFrom((Join-Path $this.BaseDir "characters"))
        $this.LoadCharactersFrom((Join-Path $this.UserDir "characters"))
        foreach ($charId in @($this.Characters.Keys)) {
            $this.Movesets[$charId] = @{}
            $this.LoadMovesFrom($charId, (Join-Path $this.BaseDir "moves\$charId"))
            $this.LoadMovesFrom($charId, (Join-Path $this.UserDir "moves\$charId"))
        }
    }

    [void] LoadCharactersFrom([string]$dir) {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { return }
        Get-ChildItem -LiteralPath $dir -Filter "*.json" -File -ErrorAction SilentlyContinue | ForEach-Object {
            $data = Read-JsonFile -Path $_.FullName
            if ($null -ne $data -and $null -ne (Get-JsonProp $data 'id' $null)) {
                $stats = [CharacterStats]::FromObject($data)
                $this.Characters[$stats.Id] = $stats
            }
        }
    }

    [void] LoadMovesFrom([string]$charId, [string]$dir) {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { return }
        Get-ChildItem -LiteralPath $dir -Filter "*.json" -File -ErrorAction SilentlyContinue | ForEach-Object {
            $data = Read-JsonFile -Path $_.FullName
            if ($null -ne $data -and $null -ne (Get-JsonProp $data 'id' $null)) {
                $move = [MoveData]::FromObject($data)
                $this.Movesets[$charId][$move.Id] = $move
            }
        }
    }

    [string[]] GetCharacterIds() {
        return @($this.Characters.Keys)
    }

    [object] GetCharacter([string]$id) {
        if ($this.Characters.ContainsKey($id)) { return $this.Characters[$id] }
        return $null
    }

    [hashtable] GetMoveset([string]$charId) {
        if ($this.Movesets.ContainsKey($charId)) { return $this.Movesets[$charId] }
        return @{}
    }

    [object] GetMove([string]$charId, [string]$moveId) {
        $ms = $this.GetMoveset($charId)
        if ($ms.ContainsKey($moveId)) { return $ms[$moveId] }
        return $null
    }

    [void] SaveCharacter([CharacterStats]$stats) {
        $this.Characters[$stats.Id] = $stats
        $path = Join-Path $this.UserDir "characters\$($stats.Id).json"
        Write-JsonFile -Path $path -Data $stats.ToHashtable()
    }

    [void] SaveMove([string]$charId, $move) {
        if (-not $this.Movesets.ContainsKey($charId)) { $this.Movesets[$charId] = @{} }
        $this.Movesets[$charId][$move.Id] = $move
        $path = Join-Path $this.UserDir "moves\$charId\$($move.Id).json"
        Write-JsonFile -Path $path -Data $move.ToHashtable()
    }
}
