@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "BUILD_DIR=build"
set "CONFIG=Release"
set "RECONFIGURE=0"
set "CLEAN=0"

:parse_args
if "%~1"=="" goto done_parse
if /I "%~1"=="release" set "CONFIG=Release" & shift & goto parse_args
if /I "%~1"=="debug" set "CONFIG=Debug" & shift & goto parse_args
if /I "%~1"=="--release" set "CONFIG=Release" & shift & goto parse_args
if /I "%~1"=="--debug" set "CONFIG=Debug" & shift & goto parse_args
if /I "%~1"=="--reconfigure" set "RECONFIGURE=1" & shift & goto parse_args
if /I "%~1"=="--clean" set "CLEAN=1" & shift & goto parse_args
if /I "%~1"=="--help" goto show_help
if /I "%~1"=="-h" goto show_help
if /I "%~1"=="/?" goto show_help

echo Unknown option: %~1
echo Run build.bat --help for usage.
exit /b 1

:done_parse

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake not found. Install CMake and add it to PATH.
    exit /b 1
)

if not exist "extern\entt\src\entt\entt.hpp" (
    echo EnTT not found. Running setup_entt.ps1...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_entt.ps1"
    if errorlevel 1 (
        echo ERROR: EnTT setup failed.
        exit /b 1
    )
)

if "%CLEAN%"=="1" (
    if exist "%BUILD_DIR%" (
        echo Cleaning %BUILD_DIR%...
        rmdir /s /q "%BUILD_DIR%"
    )
    set "RECONFIGURE=1"
)

if not exist "%BUILD_DIR%\CMakeCache.txt" set "RECONFIGURE=1"

if "%RECONFIGURE%"=="1" (
    echo Configuring CMake ^(%CONFIG%^)...
    cmake -S . -B "%BUILD_DIR%" -A x64 -DCMAKE_BUILD_TYPE=%CONFIG%
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        echo Ensure Visual Studio, the Vulkan SDK, and Qt MSVC 64-bit are installed.
        exit /b 1
    )
)

echo Building ^(%CONFIG%^)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo Build succeeded.
echo   HephaestusEngine: %BUILD_DIR%\%CONFIG%\HephaestusEngine.exe
echo   HephaestusLauncher: %BUILD_DIR%\%CONFIG%\HephaestusLauncher.exe
exit /b 0

:show_help
echo Usage: build.bat [release^|debug] [--reconfigure] [--clean]
echo.
echo Options:
echo   release         Build Release configuration ^(default^)
echo   debug           Build Debug configuration
echo   --reconfigure   Force CMake to re-run configure
echo   --clean         Delete the build directory before configuring
echo   --help          Show this help message
echo.
echo Examples:
echo   build.bat
echo   build.bat debug
echo   build.bat --clean --reconfigure
exit /b 0
