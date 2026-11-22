@echo off
rem Makes res\pgo\d2k.profdata (the profile build\d2k.exe is optimised with, CMakeLists.txt): an instrumented d2k.exe
rem in build-pgo-gen, run headless on the boot and title screens and on the bench states (Tools\d2k\bench.ps1's; they
rem live in logs\d2k, not in git), plus one window run for the drawing; then the profiles merged and build\ rebuilt.
rem Run from PowerShell: cmd /c Tools\d2k\pgo.bat
setlocal
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin;%PATH%"
cd /d "%~dp0..\.."
if not exist build-pgo-gen\build.ninja cmake -S . -B build-pgo-gen -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DD2K_PGO_GENERATE=ON || exit /b 1
cmake --build build-pgo-gen --target d2k || exit /b 1
if exist logs\pgo rmdir /s /q logs\pgo
mkdir logs\pgo
set "LLVM_PROFILE_FILE=%CD%\logs\pgo\d2k-%%p.profraw"
set "EXE=build-pgo-gen\d2k.exe"
set "UD=--userdata=logs\pgo-userdata"
%EXE% --headless %UD% --frames=3300 || exit /b 1
for %%s in (logs\d2k\black\owner_slot1.state logs\d2k\aurora\owner.state logs\d2k\classic1800.state logs\d2k\plus1800.state logs\d2k\bonus\owner_bonus.state logs\d2k\hang\owner_l2end.state) do (
  %EXE% --headless %UD% --state=%%s --frames=1500 || exit /b 1
)
start /wait "" %EXE% %UD% --state=logs\d2k\black\owner_slot1.state --winsize=1280x800 --winshot=900:logs\pgo\w.bmp
if not exist res\pgo mkdir res\pgo
llvm-profdata merge -output=res\pgo\d2k.profdata logs\pgo\*.profraw || exit /b 1
cmake --build build --target d2k
