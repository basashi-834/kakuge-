# Data/JsonHelpers.ps1
# Small helpers around PowerShell's BUILT-IN ConvertTo-Json/ConvertFrom-Json
# cmdlets (Microsoft.PowerShell.Utility - always present, no assembly to
# load, no extra install). ConvertFrom-Json returns a PSCustomObject whose
# properties must be probed defensively (a missing JSON key is simply an
# absent property, not $null), which is what Get-JsonProp is for - it
# mirrors the `dict.get(key, default)` pattern used throughout the design.

function Get-JsonProp {
    param(
        [Parameter(Mandatory = $false)] $Obj,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $false)] $Default = $null
    )
    if ($null -eq $Obj) { return $Default }
    $prop = $Obj.PSObject.Properties[$Name]
    if ($null -eq $prop) { return $Default }
    if ($null -eq $prop.Value) { return $Default }
    return $prop.Value
}

# Always returns a PowerShell array, even for a single JSON object/scalar,
# so callers never have to special-case "was it an array or not".
function ConvertTo-PSArray {
    param($Value)
    if ($null -eq $Value) { return @() }
    if ($Value -is [array]) { return $Value }
    return @($Value)
}

function Read-JsonFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($text)) { return $null }
    return ($text | ConvertFrom-Json -ErrorAction Stop)
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Data
    )
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    ($Data | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $Path -Encoding UTF8
}
