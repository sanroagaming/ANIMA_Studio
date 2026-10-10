@echo off
setlocal
set "ROOT=%~dp0"
if exist "%ROOT%AnimaStudio.exe" (
  start "" "%ROOT%AnimaStudio.exe"
  exit /b 0
)
if exist "%ROOT%x64\Release\AnimaStudio.exe" (
  start "" "%ROOT%x64\Release\AnimaStudio.exe"
  exit /b 0
)
if exist "%ROOT%x64\Debug\AnimaStudio.exe" (
  start "" "%ROOT%x64\Debug\AnimaStudio.exe"
  exit /b 0
)
echo ANIMA Studio was not found.
echo Compile the full ANIMA_Studio.sln, or run Publish-ANIMA-Beta.bat to create a portable package.
pause
exit /b 2
