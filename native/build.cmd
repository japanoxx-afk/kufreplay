@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist dist\native mkdir dist\native
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /MT /LD native\engine.cpp /Fo:dist\native\engine.obj /Fe:dist\native\KUFReplayEngine.dll /link user32.lib
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /MT native\injector.cpp /Fo:dist\native\injector.obj /Fe:dist\native\KUFReplayInjector.exe
if errorlevel 1 exit /b 1
exit /b 0
