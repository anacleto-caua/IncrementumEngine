@echo off
rem Thesis figure: three plain OpenSimplex2 octaves (f, 2f, 4f at amplitudes 1, 0.5, 0.25) and
rem their sum, written to imagens\oitavas\. Standalone - compiles tools\octaves.cpp with clang++
rem directly, no engine build, no window.
rem
rem No delayed expansion on purpose: the repo path contains "!", which delayed expansion eats.

setlocal
cd /d "%~dp0"

if not exist build\tools mkdir build\tools
clang++ -std=c++23 -O2 -w -isystem libs tools\octaves.cpp -o build\tools\octaves.exe
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)
build\tools\octaves.exe imagens\oitavas
