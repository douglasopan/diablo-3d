@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Iniciar-Tristram.ps1" -MeshyReview
if errorlevel 1 pause
