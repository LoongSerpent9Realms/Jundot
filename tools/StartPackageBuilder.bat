@echo off
cd /d "PackageBuilder\bin\CodexCheck3\" || exit /b
start "" "JundotPackageBuilder.exe" --ai-package-builder
exit