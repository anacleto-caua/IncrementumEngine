@echo off
rem Exports one terrain chunk as three equally sized square images (see docs/screenshots.md,
rem "Chunk shots"):
rem   1_heightmap.png          its heightmap, top-down, stretched to the chunk's own min..max
rem   2_chunk_isolated.png     only that chunk drawn, floating in space
rem   3_chunk_in_context.png   same camera, the whole world, with props
rem All three are --shot-chunk-size pixels square (default 1024).
rem
rem   chunkshot.cmd                                   default chunk (3,2)
rem   chunkshot.cmd --shot-chunk=3,-2                 chunk grid position x,z (ring 0, 100 units each)
rem   chunkshot.cmd --shot-chunk=3,-2 --shot-chunk-yaw=135 --shot-chunk-distance=250
rem
rem Every argument goes to the engine untouched (%*, never shift: cmd splits arguments at "," and
rem "=", which would break both of the forms above). Builds release and writes everything into
rem   screenshots\chunk_<yyyyMMdd_HHmmss>\
rem Don't minimize or cover the window while it runs.
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
set "OUT=%~dp0screenshots\chunk_%STAMP%"

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
"%EXE%" "--screenshots=%OUT%" --shot-set=chunk %*
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
