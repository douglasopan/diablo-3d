@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Godot-Editor.ps1" -Acao Testar %*
if errorlevel 1 pause
