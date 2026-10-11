@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist build\native mkdir build\native
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /MT native\engine_test.cpp /Fo:build\native\engine_test.obj /Fe:build\native\engine_test.exe /link user32.lib
if errorlevel 1 exit /b 1
build\native\engine_test.exe
exit /b %errorlevel%
