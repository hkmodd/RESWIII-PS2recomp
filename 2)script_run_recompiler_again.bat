@echo off
echo Starting PS2 xRecomp...
echo.

build_clang\ps2xRecomp\Release\ps2_recomp.exe test_config.toml --use-ir

echo.
if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Recompilation finished.
) else (
    echo [ERROR] Recompilation failed with exit code %ERRORLEVEL%.
)
pause
