@echo off
setlocal
cd /d "%~dp0"
call build.bat --no-pause
if errorlevel 1 exit /b 1
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "build\safetyhook\tests" mkdir "build\safetyhook\tests"
cl /nologo /std:c++latest /utf-8 /O2 /MT /W4 /EHsc /DVCE_TEST ^
 /DZYDIS_STATIC_BUILD /DZYCORE_STATIC_BUILD /Ithird_party\safetyhook ^
 MHWI-VisualControllerExtended.cpp build\safetyhook\safetyhook.obj build\safetyhook\Zydis.obj ^
 /Fo:build\safetyhook\tests\rules.obj /Fe:build\safetyhook\tests\rules.exe /link psapi.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /utf-8 /O2 /MT /W4 /EHsc ^
 /DZYDIS_STATIC_BUILD /DZYCORE_STATIC_BUILD /Ithird_party\safetyhook ^
 tests\safetyhook_smoke.cpp build\safetyhook\safetyhook.obj build\safetyhook\Zydis.obj ^
 /Fo:build\safetyhook\tests\smoke.obj /Fe:build\safetyhook\tests\smoke.exe /link psapi.lib user32.lib
if errorlevel 1 exit /b 1
copy /y test_rules.ini build\safetyhook\tests\test_rules.ini >nul
if errorlevel 1 exit /b 1
pushd build\safetyhook\tests
rules.exe >rules-results.txt 2>&1
set "RULES_RESULT=%ERRORLEVEL%"
smoke.exe >smoke-results.txt 2>&1
set "SMOKE_RESULT=%ERRORLEVEL%"
type rules-results.txt
type smoke-results.txt
popd
if not "%RULES_RESULT%"=="0" exit /b 1
if not "%SMOKE_RESULT%"=="0" exit /b 1
exit /b 0
