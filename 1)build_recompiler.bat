if not exist logs mkdir logs
echo Building ps2_recomp... Logging to logs\build_recompiler.log
cmake --build build_clang --target ps2_recomp --config Release -j 14 > logs\build_recompiler.log 2>&1
echo Finito. Controlla logs\build_recompiler.log se ci sono errori.
pause