# Our renderer's window picture from two d2k.exe builds, compared pixel for pixel: from each bench state, a --winshot
# at a few frames with the blend time fixed (D2K_FORCE_A), and the CRT off (its trails follow the clock), so the pictures are repeatable. For checking
# that a speed-up changed nothing. Needs the states in logs\d2k (not in git).
#   powershell -File Tools\d2k\same_picture.ps1 -Ref <old d2k.exe> [-Exe build\d2k.exe]
param([Parameter(Mandatory)][string]$Ref, [string]$Exe = "build\d2k.exe", [int[]]$At = @(200, 457, 731))
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$states = @("logs\d2k\black\owner_slot1.state", "logs\d2k\aurora\owner.state", "logs\d2k\classic1800.state",
            "logs\d2k\plus1800.state", "logs\d2k\bonus\owner_bonus.state", "logs\d2k\hang\owner_l2end.state")
$out = "logs\same-picture"
New-Item -ItemType Directory -Force $out | Out-Null
$ud = "logs\bench-userdata"
$env:D2K_FORCE_A = "0.5"
$bad = 0
foreach ($s in $states) {
  foreach ($f in $At) {
    $n = [IO.Path]::GetFileNameWithoutExtension($s) + "_$f"
    foreach ($pair in @(@($Ref, "a"), @($Exe, "b"))) {
      Start-Process -Wait -FilePath $pair[0] -ArgumentList "--userdata=$ud", "--state=$s", "--winsize=1280x800", "--set=crt=0",
        "--winshot=${f}:$out\${n}_$($pair[1]).bmp"
    }
    $a = [IO.File]::ReadAllBytes("$out\${n}_a.bmp"); $b = [IO.File]::ReadAllBytes("$out\${n}_b.bmp")
    $same = $a.Length -eq $b.Length
    if ($same) { for ($i = 0; $i -lt $a.Length; ++$i) { if ($a[$i] -ne $b[$i]) { $same = $false; break } } }
    if (-not $same) { ++$bad }
    "{0,-22} {1}" -f $n, $(if ($same) { "same" } else { "DIFFERENT" })
  }
}
Remove-Item Env:D2K_FORCE_A
"$bad different"
