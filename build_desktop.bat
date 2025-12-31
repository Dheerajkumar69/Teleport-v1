@echo off
REM Configure and build Teleport Desktop UI

REM Setup Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64

REM Navigate to project
cd /d "D:\CODES\actual projects\Teleport"

REM Clean build folder
if exist build rmdir /s /q build

REM Configure with CMake
cmake -B build -S . -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

REM Build the UI target
cmake --build build --target TeleportUI
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Build successful!
echo Executable at: build\desktop\Teleport.exe
pause
