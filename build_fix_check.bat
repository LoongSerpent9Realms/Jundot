@echo off
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -C . -j8 2>&1 | findstr /C:"error" /C:"warning" /C:"ninja: error"