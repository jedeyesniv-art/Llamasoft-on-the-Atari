# "Llamasoft on the Atari" (the project's title from 3.0.0; "Llamasoft on the ST" from 1.3.0; the exe stays llamatron.exe).
# Builds Release and assembles the portable folder dist\LlamasoftOnTheAtari\:
#   llamatron.exe (static runtime: no DLLs), README.txt, licenses\, game\ with every game (see below), Extras\. The PDB
#   goes to dist\symbols\.
# -Release also writes dist\release\LlamasoftOnTheAtari-<version>-windows.zip (top folder "Llamasoft on the Atari", with
# game\; no unpacked game files besides Trip-A-Tron's folder, no userdata or logs - checked) and
# LlamasoftOnTheAtari-<version>-symbols.zip.
#
# The games (owner 2026-11-01: Llamasoft only - the compilation disks' other games don't ship):
#   Llamatron ST.msa (Llamatron + Super Gridrunner), Trip-A-Tron\ (TRIP_A_T.RON's files), the Revenge 1 MB STE
#   .stx, and Andes Attack / Defender 2 / Hardcore .st built by Tools\make_release_disks.py from the downloads.
# Defender 2000 (Atari Jaguar, from 2026-11-22): d2k.exe next to llamatron.exe (GPL-3: the Virtual Jaguar core), its
#   licence, the cartridge in game\ and the high-res art pack (d2k_art\, made from it) - in the release too (owner
#   2026-11-22: "this game is very old and on a system that is publicly accessible thanks to its obsolescence").
#
# Usage: powershell -ExecutionPolicy Bypass -File Tools\package_portable.ps1 [-SkipBuild] [-Release] [-Folder <name>]
# (-Folder: another folder under dist\ for the portable build, e.g. a test build next to the release one.)
param(
    [switch]$SkipBuild,
    [switch]$Release,
    [string]$Folder = "LlamasoftOnTheAtari"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo "dist\$Folder"
$symbols = Join-Path $repo "dist\symbols"

if (-not $SkipBuild) {
    $log = Join-Path $repo "build\package_build.log"
    New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
    cmd /c "`"$repo\build.bat`" > `"$log`" 2>&1"
    if (Select-String -Path $log -Pattern "FAILED|error:" -Quiet) {
        Get-Content $log | Select-String -Pattern "FAILED|error:" | Select-Object -First 20
        throw "Release build failed; see $log"
    }
    Write-Host "Release build OK"
}

$exe = Join-Path $repo "build\llamatron.exe"
if (-not (Test-Path $exe)) { throw "$exe not found - build first" }
$d2k = Join-Path $repo "build\d2k.exe"
if (-not (Test-Path $d2k)) { throw "$d2k not found - build first" }
$d2kCart = Join-Path $repo "Private\d2k\Defender 2000 (1996).jag"
$d2kArt = Join-Path $repo "Private\d2k\d2k_art"

# Defender 2000: the cartridge into game\ and the art pack next to the exes. Both required.
function Copy-D2k([string]$root) {
    if (-not (Test-Path -LiteralPath $d2kCart)) { throw "$d2kCart not found" }
    if (-not (Test-Path (Join-Path $d2kArt "pack.txt"))) { throw "$d2kArt\pack.txt not found (Tools\d2k\d2k_art_pack.py)" }
    Copy-Item -LiteralPath $d2kCart (Join-Path $root "game")
    Copy-Item -Recurse $d2kArt (Join-Path $root "d2k_art")
}

# The licences: ours (third-party notices) and the Virtual Jaguar core's GPL-3 (d2k.exe).
function Copy-Licenses([string]$to) {
    New-Item -ItemType Directory -Force $to | Out-Null
    Copy-Item (Join-Path $repo "licenses\THIRD-PARTY-NOTICES.txt") $to
    Copy-Item (Join-Path $repo "third_party\virtualjaguar\LICENSE") (Join-Path $to "VIRTUAL-JAGUAR-GPL-3.txt")
}
$version = (Select-String -Path (Join-Path $repo "CMakeLists.txt") -Pattern 'project\(Llamatron VERSION ([0-9.]+)').Matches[0].Groups[1].Value

# The game folder's contents, assembled once.
function Copy-Games([string]$to) {
    New-Item -ItemType Directory -Force $to | Out-Null
    $llamatron = Join-Path $repo "Private\Llamatron ST.msa"
    $trip = Join-Path $repo "Private\new\trip_a_t\TRIP_A_T.RON"
    $rmc = Get-ChildItem (Join-Path $repo "Private\new\rmc1mb") -Filter *.stx | Select-Object -First 1
    foreach ($p in $llamatron, $trip) { if (-not (Test-Path $p)) { throw "$p not found" } }
    if (-not $rmc) { throw "the Revenge 1 MB STE .stx (Private\new\rmc1mb) not found" }
    Copy-Item $llamatron $to
    New-Item -ItemType Directory -Force (Join-Path $to "Trip-A-Tron") | Out-Null
    Copy-Item (Join-Path $trip "*") (Join-Path $to "Trip-A-Tron")
    # Its manual as text (X on the disks): made from Private\trip-a-tron-manual.pdf by make_trip_manual.py.
    python -P (Join-Path $repo "Tools\make_trip_manual.py") (Join-Path $repo "Private\trip-a-tron-manual.pdf") (Join-Path $to "Trip-A-Tron\MANUAL.TXT")
    if ($LASTEXITCODE) { throw "make_trip_manual.py failed (needs pdftotext and Private\trip-a-tron-manual.pdf)" }
    # Docs found elsewhere (owner 2026-11-08: Photon Storm's manual, typed in by Parasite of LSD): res\docs ->
    # game\docs, where the app looks for a game's docs_text when its disk doesn't have it.
    New-Item -ItemType Directory -Force (Join-Path $to "docs") | Out-Null
    Copy-Item (Join-Path $repo "res\docs\*.TXT") (Join-Path $to "docs")
    # -LiteralPath: the download's name has [brackets], which -Path takes as a wildcard (and copies nothing).
    Copy-Item -LiteralPath $rmc.FullName -Destination (Join-Path $to "Revenge of the Mutant Camels (1MB STE).stx")
    # The TT Megamix (owner 2026-11-01: include it, "a great historic artefact"): its disk holds only Revenge.
    $rmctt = Join-Path $repo "Private\Revenge_of_the_Mutant_Camels_demo-playable_1991_Llamasoft_TT_Megamix.st"
    if (-not (Test-Path -LiteralPath $rmctt)) { throw "$rmctt not found" }
    Copy-Item -LiteralPath $rmctt -Destination (Join-Path $to "Revenge of the Mutant Camels (TT Megamix).st")
    # Llamazap (Falcon030; owner 2026-11-20: in the release - public for years, of no commercial interest): the
    # 1.44 MB disk from Private\llamazap_[falcon030]_atari_(uk).zip, unpacked to Private\new\llamazap\Llamazap.st.
    $zap = Join-Path $repo "Private\new\llamazap\Llamazap.st"
    if (-not (Test-Path -LiteralPath $zap)) { throw "$zap not found (unzip the .ST from Private\llamazap_[falcon030]_atari_(uk).zip there)" }
    Copy-Item -LiteralPath $zap -Destination (Join-Path $to "Llamazap (Falcon030).st")
    python -P (Join-Path $repo "Tools\make_release_disks.py") $to
    if ($LASTEXITCODE) { throw "make_release_disks.py failed" }
    foreach ($f in "Llamatron ST.msa", "Trip-A-Tron\TRIP1.TOS", "Trip-A-Tron\MANUAL.TXT", "docs\PHOTONSTORM.TXT", "docs\LLAMAZAP.TXT", "Revenge of the Mutant Camels (1MB STE).stx",
                   "Revenge of the Mutant Camels (TT Megamix).st", "Llamazap (Falcon030).st",
                   "Andes Attack.st", "Defender 2.st", "Hardcore.st", "Photon Storm.st") {
        if (-not (Test-Path -LiteralPath (Join-Path $to $f))) { throw "game\$f missing" }
    }
}

# Extras\: things to read outside the app (owner 2026-11-07): Trip-A-Tron's whole manual as the PDF (James "Piku"
# Grimwood's 2003 transcription; the app shows it as text with X).
function Copy-Extras([string]$to) {
    $pdf = Join-Path $repo "Private\trip-a-tron-manual.pdf"
    if (-not (Test-Path $pdf)) { throw "$pdf not found" }
    New-Item -ItemType Directory -Force $to | Out-Null
    Copy-Item $pdf (Join-Path $to "Trip-A-Tron manual.pdf")
    # Steam library art (Tools\make_steam_art.py -> res\steam): Settings, ADD TO STEAM copies it into Steam.
    New-Item -ItemType Directory -Force (Join-Path $to "Steam art") | Out-Null
    foreach ($f in "capsule.png", "wide.png", "hero.png", "logo.png") {
        Copy-Item (Join-Path $repo "res\steam\$f") (Join-Path $to "Steam art")
    }
    # CRT+ / CRT++ as RetroArch slang presets for ShaderGlass / RetroArch (owner 2026-11-08; crt-shaders\, MIT).
    $shaders = Join-Path $to "CRT shaders"
    Copy-Item -Recurse (Join-Path $repo "crt-shaders") $shaders
    foreach ($f in "crt-plus.slangp", "crt-plus-plus.slangp", "README.md", "LICENSE", "shaders\crt.inc") {
        if (-not (Test-Path (Join-Path $shaders $f))) { throw "Extras\CRT shaders\$f missing" }
    }
}

# Fresh folder, but keep userdata\ if it's already there (the owner's scores and settings).
New-Item -ItemType Directory -Force $dist | Out-Null
Get-ChildItem $dist | Where-Object { $_.Name -ne "userdata" } | Remove-Item -Recurse -Force
Copy-Item $exe, $d2k $dist
Copy-Item (Join-Path $repo "res\README.txt") $dist
Copy-Licenses (Join-Path $dist "licenses")
Copy-Games (Join-Path $dist "game")
Copy-D2k $dist
Copy-Extras (Join-Path $dist "Extras")
New-Item -ItemType Directory -Force $symbols | Out-Null
Copy-Item (Join-Path $repo "build\llamatron.pdb"), (Join-Path $repo "build\d2k.pdb") $symbols
Write-Host "portable folder: $dist (version $version)"

if ($Release) {
    $out = Join-Path $repo "dist\release"
    New-Item -ItemType Directory -Force $out | Out-Null
    $stage = Join-Path $out "stage\Llamasoft on the Atari"
    if (Test-Path (Split-Path $stage)) { Remove-Item -Recurse -Force (Split-Path $stage) }
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item $exe, $d2k, (Join-Path $repo "res\README.txt") $stage
    Copy-Licenses (Join-Path $stage "licenses")
    Copy-Games (Join-Path $stage "game")
    Copy-D2k $stage
    Copy-Extras (Join-Path $stage "Extras")
    # Leak check: disk images (and Defender 2000's cartridge) only (plus Trip-A-Tron's own folder, which is how it
    # ships); no unpacked game files from the other disks, no user data (scores, save states), no logs.
    $bad = Get-ChildItem -Recurse $stage | Where-Object {
        ($_.Extension -in ".prg", ".tos", ".yak", ".cow", ".bin", ".log", ".boy" -and $_.FullName -notmatch "\\Trip-A-Tron\\") -or
        $_.Extension -in ".eeprom", ".state" -or $_.FullName -match "\\userdata" }
    if ($bad) { throw "release would contain: $($bad.FullName -join ', ')" }
    $zip = Join-Path $out "LlamasoftOnTheAtari-$version-windows.zip"
    Compress-Archive -Path $stage -DestinationPath $zip -Force
    Compress-Archive -Path (Join-Path $symbols "llamatron.pdb"), (Join-Path $symbols "d2k.pdb") -DestinationPath (Join-Path $out "LlamasoftOnTheAtari-$version-symbols.zip") -Force
    Remove-Item -Recurse -Force (Split-Path $stage)
    Write-Host "release: $zip"
}
