@echo off
REM Runs phase3.exe on the LARGE TEST database (smartcare_test), not your real data.
cd /d "%~dp0"
if exist config\local_env.bat call config\local_env.bat
set PGDATABASE=smartcare_test
phase3.exe
pause
