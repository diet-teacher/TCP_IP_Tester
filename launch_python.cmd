@echo off
setlocal
cd /d "%~dp0"
set "bundledPython=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%bundledPython%" (
  "%bundledPython%" "%~dp0python\image_peer.py"
) else (
  py -3 "%~dp0python\image_peer.py"
)
if errorlevel 1 (
  echo Install Python 3 with Tkinter and Pillow: py -3 -m pip install -r python\requirements.txt
  pause
)
