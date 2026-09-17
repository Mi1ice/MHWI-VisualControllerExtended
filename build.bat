@echo off
setlocal
rem SafetyHook build. Requires MSVC with C++23 support; no CMake needed.
cd /d "%~dp0"
if not exist "build\safetyhook" mkdir "build\safetyhook"
if errorlevel 1 goto :failed

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VCVARS (
    echo [ERROR] Visual Studio C++ x64 tools not found.
    goto :failed
)
echo [build] Using: %VCVARS%
call "%VCVARS%" >nul
if errorlevel 1 goto :failed

rem Compile Zydis as C separately; compile plugin and SafetyHook as C++23.
cl /nologo /TC /utf-8 /O2 /MT /W3 /c ^
   /D NDEBUG /D ZYDIS_STATIC_BUILD /D ZYCORE_STATIC_BUILD ^
   /Ithird_party\safetyhook third_party\safetyhook\Zydis.c ^
   /Fo:build\safetyhook\Zydis.obj
if errorlevel 1 goto :failed

cl /nologo /std:c++latest /utf-8 /O2 /MT /W4 /EHsc /LD ^
   /D WIN32_LEAN_AND_MEAN /D NDEBUG /D ZYDIS_STATIC_BUILD /D ZYCORE_STATIC_BUILD ^
   /Ithird_party\safetyhook MHWI-VisualControllerExtended.cpp ^
   third_party\safetyhook\safetyhook.cpp build\safetyhook\Zydis.obj ^
   /Fo:build\safetyhook\ /Fe:build\safetyhook\MHWI-VisualControllerExtended.dll ^
   /link psapi.lib user32.lib
if errorlevel 1 goto :failed

echo [success] %CD%\build\safetyhook\MHWI-VisualControllerExtended.dll
echo [backend] SafetyHook 0.7.0 / Zydis 4.1.0
if /i not "%~1"=="--no-pause" pause
exit /b 0

:failed
echo [ERROR] SafetyHook build failed.
if /i not "%~1"=="--no-pause" pause
exit /b 1
