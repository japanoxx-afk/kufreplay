@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
set "nativeOutput=dist\native"
if not "%~1"=="" set "nativeOutput=%~1"
if not exist "%nativeOutput%" mkdir "%nativeOutput%"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /MT /LD native\engine.cpp /Fo:"%nativeOutput%\engine.obj" /Fe:"%nativeOutput%\KUFReplayEngine.dll" /link user32.lib
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /MT native\injector.cpp /Fo:"%nativeOutput%\injector.obj" /Fe:"%nativeOutput%\KUFReplayInjector.exe"
if errorlevel 1 exit /b 1
exit /b 0
