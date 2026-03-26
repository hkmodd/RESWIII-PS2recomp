@echo off
echo Configuring CMake with Clang-CL (Visual Studio Clang Toolset)...
mkdir build_clang >nul 2>&1
cd build_clang

:: Use the Visual Studio generator with clang-cl toolset
cmake .. -G "Visual Studio 17 2022" -T ClangCL

if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration with Clang-CL failed!
    echo Ensure you have "C++ Clang tools for Windows" installed in Visual Studio Installer.
    exit /b %errorlevel%
)

echo Building with Clang-CL...
cmake --build . --config Release -j 12

echo Done!
pause
