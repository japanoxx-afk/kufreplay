@echo off
cd /d "%~dp0"
py -3 src\launcher.py
if errorlevel 1 pause
