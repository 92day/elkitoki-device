@echo off
setlocal

cd /d "%~dp0"

if not exist venv (
  echo [helmet-gate] creating virtual environment...
  python -m venv venv
)

call venv\Scripts\activate.bat

echo [helmet-gate] installing dependencies...
python -m pip install --upgrade pip
pip install -r requirements.txt

echo.
echo [helmet-gate] setup complete.
echo Next step: run_helmet_gate.bat
echo.
pause
