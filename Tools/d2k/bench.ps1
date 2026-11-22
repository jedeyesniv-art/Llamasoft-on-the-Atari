# Headless speed of d2k.exe from a few save states (2000 play, Classic, Plus, the bonus tunnel, a city level, the
# level-end fill): frames a second each, best of -Runs. Needs the states in logs\d2k (not in git).
#   powershell -File Tools\d2k\bench.ps1 [-Exe build\d2k.exe] [-Frames 1500] [-Runs 2]
param([string]$Exe = "build\d2k.exe", [int]$Frames = 1500, [int]$Runs = 2)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$states = [ordered]@{
  "2000 play"  = "logs\d2k\black\owner_slot1.state"
  "2000 city"  = "logs\d2k\aurora\owner.state"
  "classic"    = "logs\d2k\classic1800.state"
  "plus"       = "logs\d2k\plus1800.state"
  "bonus"      = "logs\d2k\bonus\owner_bonus.state"
  "level end"  = "logs\d2k\hang\owner_l2end.state"
}
$ud = "logs\bench-userdata"
New-Item -ItemType Directory -Force $ud | Out-Null
$total = 0.0
foreach ($name in $states.Keys) {
  $best = 0.0
  for ($r = 0; $r -lt $Runs; ++$r) {
    $out = & $Exe --headless "--userdata=$ud" "--state=$($states[$name])" "--frames=$Frames" 2>&1 | Out-String
    if ($out -match '\(([0-9.]+) fps') { $best = [math]::Max($best, [double]$Matches[1]) }
  }
  $total += 1000.0 / $best
  "{0,-10} {1,6:N0} fps  {2,6:N2} ms a frame" -f $name, $best, (1000.0 / $best)
}
"{0,-10} {1,6:N2} ms a frame (mean)" -f "all", ($total / $states.Count)
