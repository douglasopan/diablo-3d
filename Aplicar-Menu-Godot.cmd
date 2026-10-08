@echo off
if exist "%~dp0devilutionx\tools\Menu-Godot.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0devilutionx\tools\Menu-Godot.ps1" -Acao Aplicar
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Menu-Godot.ps1" -Acao Aplicar
)
pause
