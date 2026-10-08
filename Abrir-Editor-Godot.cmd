@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Godot-Editor.ps1" -Acao Abrir %*
if errorlevel 1 pause
