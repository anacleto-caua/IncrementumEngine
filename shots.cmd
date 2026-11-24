@echo off
rem Captures the four rendering-stage screenshots (see docs/screenshots.md):
rem   1_terrain_debug.png        chunk debug colors, no props, no post pass
rem   2_terrain_textured.png     procedural terrain shading, no props, no post pass
rem   3_terrain_props.png        + instanced props
rem   4_terrain_props_post.png   + post pass (exposure + ACES tonemap) - the normal final image
rem
rem   shots.cmd                                  default view, 1920x1080
rem   shots.cmd --shot-yaw=135 --shot-pitch=-30  any --shot-* argument is passed through
rem
rem Builds release, runs the engine once, and writes the PNGs into
rem   screenshots\<yyyyMMdd_HHmmss>\
rem The engine waits for the streamed world to finish generating before the first shot, so a run
rem takes roughly 10-30 s. Don't minimize or cover the window while it runs.
rem
rem No delayed expansion on purpose: the repo path contains "!", which delayed expansion eats.

setlocal
cd /d "%~dp0"

echo === Building release ===
rem -a x64 explicitly: from some shells xmake auto-detects arch x86_64 and then can't find clang.
call xmake f -m release -p windows -a x64 -y
if errorlevel 1 goto :build_failed
call xmake
if errorlevel 1 goto :build_failed

for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%i"
set "OUT=%~dp0screenshots\%STAMP%"

rem The engine loads shaders/assets relative to the working directory. The exe itself is called by
rem absolute path: with NoDefaultCurrentDirectoryInExePath set, cmd won't run it from the current
rem directory by bare name.
set "BIN_DIR=%~dp0build\windows\release"
set "EXE=%BIN_DIR%\IncrementumEngine.exe"
if not exist "%EXE%" (
    echo Built binary not found at %EXE%
    exit /b 1
)

echo === Capturing to %OUT% ===
pushd "%BIN_DIR%"
"%EXE%" "--screenshots=%OUT%" %*
set "EXIT_CODE=%ERRORLEVEL%"
popd

rem Compared as a string, not "if errorlevel 1": crash codes (access violation etc.) are negative.
if not "%EXIT_CODE%"=="0" (
    echo FAILED with exit code %EXIT_CODE% - see the log above
    exit /b 1
)
echo.
echo === Done ===
dir /b "%OUT%"
exit /b 0

:build_failed
echo Build failed - not capturing.
exit /b 1
