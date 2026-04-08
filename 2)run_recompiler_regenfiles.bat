@echo off
if not exist logs mkdir logs
echo Starting PS2 xRecomp... Logging to logs\recompiler_output.log
echo.

build_clang\ps2xRecomp\Release\ps2_recomp.exe test_config.toml --use-ir > logs\recompiler_output.log 2>&1

echo.
if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] Recompilation finished. Controlla logs\recompiler_output.log
) else (
    echo [ERROR] Recompilation failed with exit code %ERRORLEVEL%. Controlla logs\recompiler_output.log
)
pause
