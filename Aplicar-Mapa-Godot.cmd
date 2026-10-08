@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Godot-Editor.ps1" -Acao Aplicar %*
if errorlevel 1 pause
