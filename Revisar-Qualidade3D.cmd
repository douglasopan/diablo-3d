@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Iniciar-Tristram.ps1" -QualityReview %*
if errorlevel 1 pause
