@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Iniciar-Tristram.ps1" %*
if errorlevel 1 pause
