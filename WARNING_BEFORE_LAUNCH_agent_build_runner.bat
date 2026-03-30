@echo off
REM ============================================================
REM  build_runner_agent.bat — Pipeline for Recompiling and Building
REM ============================================================

set "BUILD_DIR=build_clang"
set "RECOMP_EXE=%BUILD_DIR%\ps2xRecomp\Release\ps2_recomp.exe"
set "LOG_DIR=logs"
set "BUILD_LOG=%LOG_DIR%\build_runner_log.txt"

if not exist %LOG_DIR% mkdir %LOG_DIR%

echo [1/3] Building Recompiler Generator (ps2_recomp)...
cmake --build %BUILD_DIR% --target ps2_recomp --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Recompiler build failed!
    exit /b %errorlevel%
)

echo [2/3] Generating C++ Recompiled Code (--use-ir)...
%RECOMP_EXE% test_config.toml --use-ir
if %errorlevel% neq 0 (
    echo [ERROR] Recompiler execution failed!
    exit /b %errorlevel%
)

echo [3/3] Building Entry Runner (ps2EntryRunner)...
echo            (Logging to %BUILD_LOG% to keep terminal clean)
cmake --build %BUILD_DIR% --target ps2EntryRunner --config Release --parallel > %BUILD_LOG% 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Runner build failed! See %BUILD_LOG% for details.
    exit /b %errorlevel%
)

echo [SUCCESS] Recompiler and Runner successfully built!
