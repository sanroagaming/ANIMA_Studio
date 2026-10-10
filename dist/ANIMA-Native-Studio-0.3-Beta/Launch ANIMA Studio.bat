@echo off
setlocal
set "ROOT=%~dp0"
if not exist "%ROOT%AnimaStudio.exe" (
  echo ANIMA Studio files are missing. Extract the complete portable ZIP first.
  pause
  exit /b 2
)
start "" "%ROOT%AnimaStudio.exe"
