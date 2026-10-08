@echo off
if exist "%~dp0devilutionx\tools\Menu-Godot.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0devilutionx\tools\Menu-Godot.ps1" -Acao Abrir
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Menu-Godot.ps1" -Acao Abrir
)
if errorlevel 1 pause
