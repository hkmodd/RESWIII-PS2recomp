@echo off
REM ============================================================
REM  run_game.bat — Launch PS2Recomp runner for Star Wars EP3
REM  ELF: E:\Programmi VARI\PROGETTI\RESWIII\ISO extracted\sles_531.55
REM  Runner: build64\ps2xRuntime\ps2EntryRunner.exe
REM ============================================================

set "RUNNER=E:\Programmi VARI\PROGETTI\RESWIII-PS2recomp\build_clang\ps2xRuntime\Release\ps2EntryRunner.exe"
set "ELF=E:\Programmi VARI\PROGETTI\RESWIII\ISO extracted\sles_531.55"

if not exist "%RUNNER%" (
    echo ERROR: Runner not found at %RUNNER%
    echo        Did you build? Run: cmake --build build64
    exit /b 1
)

if not exist "%ELF%" (
    echo ERROR: ELF not found at %ELF%
    exit /b 1
)

echo [run_game] Launching: %RUNNER% "%ELF%"
"%RUNNER%" "%ELF%"
