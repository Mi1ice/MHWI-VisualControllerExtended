@echo off
setlocal
rem ============================================================================
rem  MHWI-VisualControllerExtended —— 一键构建脚本
rem  直接用 cl.exe 编译（不依赖 vcxproj 工具集版本），产物在 .\build\
rem ============================================================================
cd /d "%~dp0"

if not exist "build" mkdir "build"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)

if not defined VCVARS (
    echo [ERROR] 未找到 Visual Studio C++ 工具链（vcvars64.bat）。
    echo        请安装 "使用 C++ 的桌面开发" 工作负载后重试。
    pause
    exit /b 1
)

echo [build] Using: %VCVARS%
call "%VCVARS%" >nul

cl /nologo /std:c++17 /utf-8 /O2 /MT /W4 /EHsc /LD ^
   /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D NDEBUG ^
   /Ithird_party\minhook\include /Ithird_party\minhook\src ^
   MHWI-VisualControllerExtended.cpp third_party\minhook\src\buffer.c ^
   third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c ^
   third_party\minhook\src\hde\hde64.c ^
   /Fo:build\ /Fe:build\MHWI-VisualControllerExtended.dll ^
   /link psapi.lib

if errorlevel 1 (
    echo [ERROR] 构建失败。
    pause
    exit /b 1
)

echo.
echo [success] %CD%\build\MHWI-VisualControllerExtended.dll
echo          连同 MHWI-VisualControllerExtended.ini 一起放入游戏 nativePC\plugins\ 即可。
pause
exit /b 0
