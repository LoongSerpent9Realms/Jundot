@echo off
cd /d "%~dp0PackageBuilder\bin\CodexFast\" || exit /b
start "" "JundotPackageBuilder.exe" --ai-package-builder
exit
