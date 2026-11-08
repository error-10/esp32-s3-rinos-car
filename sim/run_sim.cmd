@echo off
setlocal
cd /d "%~dp0"
if not exist "build\Release\rin_sim.exe" (
    echo rin_sim.exe not found. Build it first with CMake.
    pause
    exit /b 1
)
start "" "build\Release\rin_sim.exe"
