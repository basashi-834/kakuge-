# Character/SuperGauge.ps1
# Super meter (section 25). 0-100, gained on dealing AND receiving hits;
# supers are locked out below their MeterCost.

class SuperGauge {
    static [double] $MaxValue = 100.0
    [double]$Value = 0.0

    [void] Add([double]$amount) {
        $v = $this.Value + $amount
        if ($v -lt 0) { $v = 0 }
        if ($v -gt [SuperGauge]::MaxValue) { $v = [SuperGauge]::MaxValue }
        $this.Value = $v
    }

    [bool] CanSpend([double]$cost) {
        return $this.Value -ge $cost
    }

    [bool] Spend([double]$cost) {
        if (-not $this.CanSpend($cost)) { return $false }
        $this.Value -= $cost
        return $true
    }
}
