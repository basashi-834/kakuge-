@echo off
rem Kakuge launcher - double-click this file to start the game.
rem Uses only Windows PowerShell 5.1, which ships with every Windows 10/11
rem installation - nothing is installed or downloaded.
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Main.ps1"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Kakuge がエラー終了しました。上のメッセージと %~dp0error.log を確認してください。
    pause
)
