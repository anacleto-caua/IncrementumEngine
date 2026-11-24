@echo off
rem Unattended benchmark suite for the TCC test scenarios (test_scenerario.md).
rem
rem   bench.cmd          full suite: 5 repetitions per configuration (~2.5-3 h)
rem   bench.cmd quick    1 repetition, short durations - checks the pipeline end to end (~8 min)
rem
rem Builds release, then appends every run to ONE file:
rem   results\bench_<COMPUTERNAME>_<yyyyMMdd_HHmm>.jsonl
rem Line 1 describes the machine; every following line is one run (or a skipped/failed marker).
rem Load with: pandas.read_json(path, lines=True)
rem
rem No delayed expansion on purpose: the repo path contains "!", which delayed expansion eats.

setlocal
cd /d "%~dp0"

set "PROFILE=%~1"
if "%PROFILE%"=="" set "PROFILE=full"

if /i "%PROFILE%"=="full" goto :profile_full
if /i "%PROFILE%"=="quick" goto :profile_quick
echo usage: bench.cmd [full^|quick]
exit /b 1

:profile_full
set "REPS=5"
set "DURATION="
set "WARMUP="
set "SOAK_DURATION="
set "C1_WORKERS=1 2 4 half max-1"
goto :profile_done

:profile_quick
set "REPS=1"
set "DURATION=--duration=10"
set "WARMUP=--warmup=3"
set "SOAK_DURATION=--duration=60"
set "C1_WORKERS=1 half"
goto :profile_done

:profile_done

echo === Building release ===
rem -a x64 explicitly: from some shells xmake auto-detects arch x86_64 and then can't find clang.
call xmake f -m release -p windows -a x64 -y
if errorlevel 1 goto :build_failed
call xmake
if errorlevel 1 goto :build_failed

for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmm"') do set "STAMP=%%i"
if not exist "results" mkdir "results"
set "OUT=%~dp0results\bench_%COMPUTERNAME%_%STAMP%.jsonl"

rem The engine loads shaders/assets relative to the working directory. The exe itself is called by
rem absolute path: with NoDefaultCurrentDirectoryInExePath set, cmd won't run it from the current
rem directory by bare name.
set "BIN_DIR=%~dp0build\windows\release"
set "EXE=%BIN_DIR%\IncrementumEngine.exe"
if not exist "%EXE%" (
    echo Built binary not found at %EXE%
    exit /b 1
)
pushd "%BIN_DIR%"

echo === Machine info ===
call :run --bench=sysinfo

rem Repetitions are interleaved (every configuration once, then again) rather than run back to
rem back, so slow drift - thermals, background activity - spreads across configurations instead
rem of landing on whichever ran last.
for /l %%r in (1,1,%REPS%) do call :repetition %%r

popd
echo.
echo === Done ===
echo Results: %OUT%
exit /b 0

:repetition
echo.
echo === Repetition %1 of %REPS% ===
for %%w in (%C1_WORKERS%) do call :run --bench=c1_coldstart --workers=%%w --run=%1
for %%s in (c2_static c2_slow c2_fast c2_spin c2_spin_move) do call :run --bench=%%s --run=%1 %DURATION% %WARMUP%
call :run --bench=c3 --run=%1 --width=1280 --height=720 %DURATION% %WARMUP%
call :run --bench=c3 --run=%1 --width=1920 --height=1080 %DURATION% %WARMUP%
rem One soak per suite is enough - it's a stability check, and it's 15 minutes.
if "%1"=="1" call :run --bench=c4_soak --run=%1 %SOAK_DURATION% %WARMUP%
exit /b 0

:run
echo [%TIME%] IncrementumEngine.exe %*
"%EXE%" "--out=%OUT%" %*
rem Compared as a string, not "if errorlevel 1": crash codes (access violation etc.) are negative.
set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" (
    echo     FAILED with exit code %EXIT_CODE%
    >>"%OUT%" echo {"type":"run_failed","args":"%*","exit":%EXIT_CODE%}
)
exit /b 0

:build_failed
echo Build failed - not running the suite.
exit /b 1
