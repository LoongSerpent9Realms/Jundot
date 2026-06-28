@echo off
:: JundotLauncher.bat — Startup wrapper for Jundot Engine with automatic hot-update check.
::
:: Usage:
::   Double-click this file to launch Jundot with pre-launch update check.
::   Or run from command line: JundotLauncher.bat [--channel stable|beta|dev]
::
:: Flow:
::   1. Look for JundotLauncher.exe in current directory or Tools/Launcher/
::   2. If found: run launcher in "start" mode (check → update → launch)
::   3. If not found: fall back to launching the engine directly

setlocal enabledelayedexpansion

:: ── Resolve script directory ──────────────────────────────────
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: ── Find JundotLauncher ──────────────────────────────────────
set "LAUNCHER="
if exist "%SCRIPT_DIR%\JundotLauncher.exe" (
    set "LAUNCHER=%SCRIPT_DIR%\JundotLauncher.exe"
)
if exist "%SCRIPT_DIR%\Tools\Launcher\JundotLauncher.exe" (
    set "LAUNCHER=%SCRIPT_DIR%\Tools\Launcher\JundotLauncher.exe"
)

:: ── Pass through any extra arguments ─────────────────────────
set "ARGS=%*"

if not "%LAUNCHER%"=="" (
    echo [JundotLauncher] Starting with hot-update check...
    echo.
    "%LAUNCHER%" start --engine-path "%SCRIPT_DIR%" %ARGS%
    set "EXIT_CODE=%ERRORLEVEL%"
    echo.
    echo [JundotLauncher] Exited with code %EXIT_CODE%
    exit /b %EXIT_CODE%
)

:: ── Fallback: launch engine directly ──────────────────────────
echo [JundotLauncher] JundotLauncher.exe not found — launching engine directly.
echo Build the launcher first: dotnet build tools\Launcher\Launcher.csproj

:: Find the engine executable
set "ENGINE="
for %%f in ("%SCRIPT_DIR%\jundot.*.editor.*.exe") do (
    if not "%%~nxf"=="jundot.*.editor.*.exe" (
        echo %%~nxf | findstr /V ".console" >nul
        if not errorlevel 1 (
            set "ENGINE=%%f"
        )
    )
)

if "%ENGINE%"=="" (
    :: Fallback to any jundot exe
    for %%f in ("%SCRIPT_DIR%\jundot.*.exe") do (
        if not "%%~nxf"=="jundot.*.exe" (
            set "ENGINE=%%f"
        )
    )
)

if not "%ENGINE%"=="" (
    echo Starting: %ENGINE%
    start "" "%ENGINE%"
) else (
    echo ERROR: No Jundot engine executable found in %SCRIPT_DIR%
    pause
)

endlocal
