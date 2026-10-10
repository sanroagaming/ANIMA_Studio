@echo off
setlocal
set "ROOT=%~dp0"
if not "%~1"=="" goto cli
if exist "%ROOT%x64\Release\AnimaStudio.exe" (
  start "" "%ROOT%x64\Release\AnimaStudio.exe"
  exit /b 0
)
if exist "%ROOT%x64\Debug\AnimaStudio.exe" (
  start "" "%ROOT%x64\Debug\AnimaStudio.exe"
  exit /b 0
)
if exist "%ROOT%dist\ANIMA-Native-Studio-0.3-Beta\AnimaStudio.exe" (
  start "" "%ROOT%dist\ANIMA-Native-Studio-0.3-Beta\AnimaStudio.exe"
  exit /b 0
)
echo ANIMA Studio is not built yet. Run Build-ANIMA.bat or Publish-ANIMA-Beta.bat.
pause
exit /b 2
:cli
if exist "%ROOT%x64\Release\anima.exe" (
  "%ROOT%x64\Release\anima.exe" %*
  exit /b %errorlevel%
)
if exist "%ROOT%x64\Debug\anima.exe" (
  "%ROOT%x64\Debug\anima.exe" %*
  exit /b %errorlevel%
)
echo ANIMA command-line launcher was not found. Build the solution first.
pause
exit /b 2
