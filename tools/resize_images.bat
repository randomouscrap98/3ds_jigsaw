@echo off
rem Resize pictures for 3DS Jigsaw: drag a folder of pictures onto this file.
rem Or from a command prompt: resize_images.bat FOLDER [OUTFOLDER]
rem The work is done by resize_images.ps1 (next to this file).
if "%~1"=="" (
  echo Drag a folder of pictures onto this file.
  pause
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0resize_images.ps1" -InDir "%~1" -OutDir "%~2"
pause
