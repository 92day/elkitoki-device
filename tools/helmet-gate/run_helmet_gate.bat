@echo off
setlocal

cd /d "%~dp0"

if not exist venv\Scripts\activate.bat (
  echo [helmet-gate] virtual environment not found.
  echo Run setup_helmet_gate.bat first.
  pause
  exit /b 1
)

call venv\Scripts\activate.bat
python helmet_gate.py

echo.
echo [helmet-gate] program ended.
pause
