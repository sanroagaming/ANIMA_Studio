@echo off
setlocal
pushd "%~dp0..\..\x64\Debug" || (echo Build ANIMA_Studio.sln in Debug|x64 first.& exit /b 1)
if not exist AnimaLang.exe (echo AnimaLang.exe was not built.& popd& exit /b 1)
AnimaLang.exe --check AnimaLangExamples\native-power.ani || goto :fail
AnimaLang.exe --check AnimaLangExamples\definitions.ani || goto :fail
AnimaLang.exe --check AnimaLangExamples\math-and-conditions.ani || goto :fail
AnimaLang.exe AnimaLangExamples\definitions.ani || goto :fail
AnimaLang.exe AnimaLangExamples\math-and-conditions.ani || goto :fail
AnimaLang.exe AnimaLangExamples\native-power.ani || goto :fail
echo.
echo ANIMA Native 0.2 smoke tests passed.
popd
exit /b 0
:fail
echo ANIMA Native 0.2 smoke test failed with exit code %ERRORLEVEL%.
popd
exit /b 1
