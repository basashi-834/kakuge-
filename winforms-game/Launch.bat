@echo off
rem Kakuge launcher - double-click this file to start the game.
rem Uses only Windows PowerShell 5.1, which ships with every Windows 10/11
rem installation - nothing is installed or downloaded.
rem
rem NOTE: this file must stay plain ASCII only (no Japanese text) - on a
rem Japanese-locale Windows system, cmd.exe reads .bat files using the
rem Shift-JIS (CP932) codepage by default, and UTF-8 text here gets
rem misread badly enough to corrupt command parsing itself, not just the
rem displayed text. Any Japanese-language messages belong in Main.ps1's
rem MessageBox (rendered by .NET/Unicode, unaffected by console codepage)
rem or in error.log instead.
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Main.ps1"
set KAKUGE_EXIT=%ERRORLEVEL%
echo.
echo ----------------------------------------------------------------
echo Kakuge exited with code %KAKUGE_EXIT%.
echo If a window did not appear, or this closed unexpectedly, check:
echo   %~dp0error.log
echo ----------------------------------------------------------------
pause
