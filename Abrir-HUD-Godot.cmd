@echo off
if exist "%~dp0devilutionx\tools\Menu-Godot.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0devilutionx\tools\Menu-Godot.ps1" -Acao Abrir -Interface HUD
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Menu-Godot.ps1" -Acao Abrir -Interface HUD
)
if errorlevel 1 pause
