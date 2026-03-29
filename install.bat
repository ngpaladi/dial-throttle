@echo off
setlocal
set SCRIPT_DIR=%~dp0
set INSTALLER=%SCRIPT_DIR%install.py

where py >nul 2>nul
if %errorlevel%==0 (
  py -3 "%INSTALLER%" %*
  exit /b %errorlevel%
)

where python >nul 2>nul
if %errorlevel%==0 (
  python "%INSTALLER%" %*
  exit /b %errorlevel%
)

echo Error: Python 3 is required to run install.py
exit /b 1
