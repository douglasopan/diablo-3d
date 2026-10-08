@echo off
setlocal
echo Diablo 3D - Comparar apresentacao
echo 1. Nitido
echo 2. Suave
echo 3. Campo ampliado
choice /c 123 /n /m "Escolha uma opcao: "
if errorlevel 4 exit /b 1
if not errorlevel 1 exit /b 0
if errorlevel 3 (set "D3D_PRESET=Amplo") else if errorlevel 2 (set "D3D_PRESET=Suave") else (set "D3D_PRESET=Nitido")
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Iniciar-Tristram.ps1" -Presentation "%D3D_PRESET%" %*
if errorlevel 1 pause
