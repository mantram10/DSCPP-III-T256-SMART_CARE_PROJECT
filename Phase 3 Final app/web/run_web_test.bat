@echo off
REM Runs the website on the LARGE TEST database (smartcare_test), not your real data.
set PGDATABASE=smartcare_test
call "%~dp0run_web.bat"
