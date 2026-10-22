@echo off
cd /d "%~dp0"
echo ==========================================
echo        SMARTCARE PHASE II - LOCAL APP
echo ==========================================
where py >nul 2>nul
if errorlevel 1 (
  echo Python was not found. Install Python 3.10+ from https://www.python.org/downloads/
  pause
  exit /b 1
)
py -m pip install -r requirements.txt
if errorlevel 1 (
  echo Dependency installation failed.
  pause
  exit /b 1
)
echo.
echo Enter your PostgreSQL password for user postgres.
set /p SMARTCARE_DB_PASSWORD=PostgreSQL password:
set SMARTCARE_DB_NAME=smartcare
set SMARTCARE_DB_USER=postgres
set SMARTCARE_DB_HOST=localhost
set SMARTCARE_DB_PORT=5432
py server.py
pause
