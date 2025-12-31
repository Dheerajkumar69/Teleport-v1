@echo off
title Teleport CLI - Discovery
cd /d "%~dp0"
echo.
echo ========================================
echo   Teleport - Device Discovery
echo ========================================
echo.
echo Scanning for devices on your network...
echo Press Ctrl+C to stop.
echo.
teleport_cli.exe discover
pause
