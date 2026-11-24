@echo off
rem Exports top-down maps of the terrain generator's layers (see docs/screenshots.md, "Generator
rem layers"), evaluated on the CPU straight from TerrainManager's generator - nothing is rendered:
rem   1_continentes.png            continents (ocean vs land, broad plateau height)
rem   2_mascara_cordilheiras.png   range mask - where mountains are allowed
rem   3_montanhas.png              mountain field before the mask
rem   4_final.png                  the combined height the terrain heightmaps store
rem   absolute\                    the same four on one shared 0..1 scale
rem   layers_info.txt              region, per-layer value ranges, land/mountain coverage
rem
rem   layers.cmd                                       16000 x 16000 units around the origin, 1024 px
rem   layers.cmd --shot-layers-size=8000               zoom in
rem   layers.cmd --shot-layers-center=4000,-2000 --shot-layers-resolution=2048
rem
rem Every argument goes to the engine untouched (%*, never shift: cmd splits arguments at "," and
rem "="). Builds release and writes everything into
rem   screenshots\layers_<yyyyMMdd_HHmmss>\
rem The engine window opens only briefly; the export takes a few seconds.
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
set "OUT=%~dp0screenshots\layers_%STAMP%"

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
"%EXE%" "--screenshots=%OUT%" --shot-set=layers %*
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
echo Build failed - not exporting.
exit /b 1
