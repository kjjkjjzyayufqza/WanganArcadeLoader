@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Run-Loader.ps1"
exit /b %errorlevel%
