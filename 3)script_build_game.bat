@echo off
if not exist logs mkdir logs
echo Building PS2xRunner... Logging to logs\runner_build_output.log
echo.

cmake --build build_clang --config Release -j 14 > logs\build_runner.log 2>&1
echo "Finito"
pause