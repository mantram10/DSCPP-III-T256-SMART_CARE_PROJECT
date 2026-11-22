@echo off
REM Starts SmartCare (C++) with settings from config\local_env.bat
cd /d "%~dp0"
if exist config\local_env.bat call config\local_env.bat
phase3.exe
pause
