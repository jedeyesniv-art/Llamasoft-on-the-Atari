# Savestate test: for each game, run A plays the smoke test's input, saves a state at frame N (in play) and dumps
# RAM M frames later. Run B, a new process, loads that file at frame K (early: before the program has unpacked
# itself, so before any hook is in), replays A's input from N on, shifted to start at K, and dumps RAM M frames
# after the load. Run C does the same but loads into play (at L, having played A's input up to there: the hooks
# are in already). The dumps must be identical. Logs and dumps in logs\state_test\.
# usage: powershell -ExecutionPolicy Bypass -File Tools\state_test.ps1 [-Only <name>]
param([string]$Only = "")
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo "build\llamatron.exe"
$out = Join-Path $repo "logs\state_test"
New-Item -ItemType Directory -Force $out | Out-Null

# name, program, input (the smoke test's), N (save, in play), M (frames on), L (load into play, run C)
$tests = @(
    @("llamatron", "TRON1MEG.PRG", "--press=150:j1fire --press=3000:j0fire --press=3050:j1right:200 --press=3300:j1up:200", 3100, 600, 3400),
    @("gridrunner", "GRIDRUNR.PRG", "--press=600:mleft --press=1500:mleft --press=2000:mleft --press=2400:mleft --press=2500:gl0@1:300 --press=2500:grt:600", 2600, 600, 2900),
    @("tripatron", "TRIP.PRG", "--press=1500:mleft:5 --press=1700:Escape --press=1800:grt:900 --press=1800:gl0@0.5:900", 2000, 600, 2300),
    @("rmc", "REVSTE1M.TOS", "--press=300:ga:5 --press=600:ga:5 --press=900:ga:5 --press=1200:ga:5 --press=1500:ga:5 --press=1800:ga:5 --press=2100:gl0@1:1000 --press=2100:grt:1000", 2300, 800, 2600),
    @("andes", "ANDES.BOY", "--press=300:ga:5 --press=600:ga:1500 --press=600:gl0@1:1500", 900, 800, 1200),
    @("defender2", "DEFENDR2.PRG", "--press=1600:ga:3 --press=1900:ga:3 --press=1950:grt:900", 2000, 600, 2300),
    @("hardcore", "HARDCORE.PRG", "--press=600:ga:5 --press=1500:ga:800 --press=1500:gl0@0.3:800", 1600, 600, 1900),
    @("rmctt", "REVTT.TOS", "--press=300:ga:5 --press=600:ga:5 --press=900:ga:5 --press=1200:ga:5 --press=1500:ga:5 --press=1800:ga:5 --press=2100:gl0@1:1000 --press=2100:grt:1000", 2300, 800, 2600),
    @("photonstorm", "PHOTON.YAK", "--press=1500:ga:5 --press=1700:gl0@0.9:900 --press=1700:ga:900", 1800, 600, 2100),
    @("llamazap", "LLAMAZAP.PRG", "--press=300:ga:3 --press=600:ga:3 --press=900:ga:3 --press=1200:ga:3 --press=1500:ga:3 --press=1800:ga:3 --press=2100:ga:3 --press=2400:gl0@1:400 --press=2400:ga:1600 --press=2800:gl180@1:400", 2600, 1200, 2900)
)
$K = 200

# The --press list as (frame, what, frames); shifted: what was held at `from` starts at `to`, later ones move.
function Parse-Presses([string]$s) {
    [regex]::Matches($s, '--press=(\d+):([^:\s]+)(?::(\d+))?') | ForEach-Object {
        [pscustomobject]@{ F = [int]$_.Groups[1].Value; What = $_.Groups[2].Value
                           N = if ($_.Groups[3].Success) { [int]$_.Groups[3].Value } else { 3 } }
    }
}
function Before([object[]]$p, [int]$until) {  # what happens before `until`, cut there
    ($p | Where-Object { $_.F -lt $until } | ForEach-Object {
        "--press=$($_.F):$($_.What):$([Math]::Min($_.N, $until - $_.F))" }) -join " "
}
function Shifted([object[]]$p, [int]$from, [int]$to) {  # from `from` on, moved to start at `to`
    ($p | ForEach-Object {
        if ($_.F -ge $from) { "--press=$($_.F - $from + $to):$($_.What):$($_.N)" }
        elseif ($_.F + $_.N -gt $from) { "--press=$($to):$($_.What):$($_.F + $_.N - $from)" }
    }) -join " "
}
function Run([string]$name, [string]$tag, [string]$program, [string]$extra, [int]$frames) {
    $ud = Join-Path $out "ud-$name"
    $a = "--headless --program=$program --userdata=`"$ud`" --log=`"$(Join-Path $out "$name-$tag.log")`" $extra --frames=$frames"
    Start-Process -Wait $exe -ArgumentList $a
}
function Compare-Dumps([string]$x, [string]$y) {
    if (-not (Test-Path $x) -or -not (Test-Path $y)) { return "no dump" }
    $r = python -P -c "import sys,numpy as np; a=np.fromfile(sys.argv[1],np.uint8); b=np.fromfile(sys.argv[2],np.uint8); d=np.nonzero(a!=b)[0]; print('identical' if a.size==b.size and d.size==0 else '%d bytes differ, first at %s' % (d.size, ', '.join('$%X' % v for v in d[:6])))" $x $y
    return $r
}

$failed = 0
foreach ($t in $tests) {
    $name, $program, $input, $N, $M, $L = $t
    if ($Only -and $name -ne $Only) { continue }
    $state = Join-Path $out "$name.state"
    $p = @(Parse-Presses $input)
    # A: the reference (also installs Trip-A-Tron into the shared save folder before B and C run).
    Run $name "A" $program "$input --state-save=${N}:`"$state`" --dump=$($N + $M):`"$(Join-Path $out "$name-A.bin")`"" ($N + $M + 2)
    # B: loaded early.
    Run $name "B" $program "$(Shifted $p $N $K) --state-load=${K}:`"$state`" --dump=$($K + $M):`"$(Join-Path $out "$name-B.bin")`"" ($K + $M + 2)
    # C: loaded into play.
    Run $name "C" $program "$(Before $p $L) $(Shifted $p $N $L) --state-load=${L}:`"$state`" --dump=$($L + $M):`"$(Join-Path $out "$name-C.bin")`"" ($L + $M + 2)
    $b = Compare-Dumps (Join-Path $out "$name-A.bin") (Join-Path $out "$name-B.bin")
    $c = Compare-Dumps (Join-Path $out "$name-A.bin") (Join-Path $out "$name-C.bin")
    $ok = $b -eq "identical" -and $c -eq "identical"
    if (-not $ok) { $failed++ }
    "{0,-12} {1,-5} load early: {2}; load in play: {3}" -f $name, $(if ($ok) { "ok" } else { "FAIL" }), $b, $c
}
if ($failed) { "$failed failed" } else { "all identical" }
