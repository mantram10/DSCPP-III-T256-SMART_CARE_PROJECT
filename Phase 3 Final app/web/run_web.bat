@echo off
REM SmartCare Web - double-click to start. Opens http://127.0.0.1:5000
cd /d "%~dp0"
if exist "..\config\local_env.bat" (
    call "..\config\local_env.bat"
) else (
    echo NOTE: config\local_env.bat not found - cloud features will be disabled.
    echo Copy config\local_env.example.bat to config\local_env.bat and fill it in.
)
if "%PGPASSWORD%"=="" set PGPASSWORD=admin
start "" http://127.0.0.1:5000
python app.py
pause
