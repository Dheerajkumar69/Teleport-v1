@echo off
title Teleport CLI - Receive Files
cd /d "%~dp0"
echo.
echo ========================================
echo   Teleport - Receive Files
echo ========================================
echo.
echo Waiting to receive files...
echo Files will be saved to: Downloads folder
echo Press Ctrl+C to stop.
echo.
teleport_cli.exe receive
pause
