@echo off
setlocal
set "GAME_TITLE=50 Cent: Bulletproof"
set "GAME_XBE=default.xbe"
set "GAME_EXE=Bulletproof"
title %GAME_TITLE% - Build
cd /d "%~dp0"
echo.
echo   %GAME_TITLE% - build
echo   ================================================
echo.

set "PF86=%ProgramFiles(x86)%"
set "VSINST=%PF86%\Microsoft Visual Studio\Installer"
set "VSWHERE=%VSINST%\vswhere.exe"
set "OUT=%CD%\out\build\release"

if not exist assets\%GAME_XBE% goto :nogame
if not exist config\game.toml (
    echo   Restore the verified Bulletproof XDK 5849 config at config\game.toml.
    goto :fail
)

rem toolchain
echo [1/3] Toolchain
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo       Visual Studio with the C++ workload was not found.
    echo       Install Visual Studio Community and tick "Desktop development with C++".
    goto :fail
)
call :setuptools
rem CI is set on GitHub Actions: no prompts there.
if defined MISSING_VS if defined CI goto :byhand
if defined MISSING_VS (
    choice /c YN /n /m "      Let Visual Studio install the missing C++ Clang/CMake components? [Y/N] "
    if errorlevel 2 goto :byhand
    echo       Close Visual Studio if it is open, it will be shut down anyway.
    echo       Installing. Accept the admin prompt, then wait for the installer.
    powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%VSINST%\setup.exe' -ArgumentList 'modify','--installPath','%VSDIR%','--add','Microsoft.VisualStudio.Component.VC.Llvm.Clang','--add','Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset','--add','Microsoft.VisualStudio.Component.VC.CMake.Project','--passive','--norestart','--force'"
    echo       Press a key once the Visual Studio Installer window has closed.
    pause >nul
    call :setuptools
    if defined MISSING_VS goto :byhand
)
if defined MISSING_GIT goto :missinggit
for /f "tokens=*" %%v in ('clang++ --version ^| findstr /i version') do echo       %%v

echo [2/3] Configure Vulkan
cmake --preset release -DXBE_WINDOWS_PORTABLE=ON -DXBE_SHADER_BACKEND=dxc || goto :fail
echo [3/3] Compile static and portable targets
cmake --build --preset release --target Bulletproof Bulletproof_portable || goto :fail

echo.
echo   %GAME_TITLE% built.
echo   out\build\release\%GAME_EXE%.exe
echo.
if defined CI exit /b 0
choice /c YN /n /m "  Play now? [Y/N] "
if not errorlevel 2 (start "" /d "%OUT%" "%OUT%\%GAME_EXE%.exe") else (pause)
exit /b 0

rem helpers
:setuptools
set TOOLS_OK=
set MISSING_VS=
set MISSING_GIT=
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
if exist "%ProgramFiles%\LLVM\bin\clang++.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where git >nul 2>nul
if errorlevel 1 (
    if exist "%ProgramFiles%\Git\cmd\git.exe" (
        set "PATH=%ProgramFiles%\Git\cmd;%PATH%"
    ) else if exist "%ProgramFiles(x86)%\Git\cmd\git.exe" (
        set "PATH=%ProgramFiles(x86)%\Git\cmd;%PATH%"
    ) else if exist "%LocalAppData%\Programs\Git\cmd\git.exe" (
        set "PATH=%LocalAppData%\Programs\Git\cmd;%PATH%"
    )
)
where clang++ >nul 2>nul || set MISSING_VS=%MISSING_VS% clang++
where cmake   >nul 2>nul || set MISSING_VS=%MISSING_VS% cmake
where ninja   >nul 2>nul || set MISSING_VS=%MISSING_VS% ninja
where git     >nul 2>nul || set MISSING_GIT=1
if defined MISSING_VS echo       missing Visual Studio components:%MISSING_VS%
if defined MISSING_GIT echo       missing external dependency: git
if not defined MISSING_VS if not defined MISSING_GIT set TOOLS_OK=1
exit /b 0

:missinggit
echo.
echo       Git is an external dependency and is not installed by Visual Studio C++ components.
echo       Install Git for Windows, then reopen this terminal and run build.bat again:
echo         winget install --id Git.Git -e --source winget
echo         or download from https://git-scm.com/download/win
goto :fail

:byhand
echo.
if defined MISSING_VS (
    echo       Install missing Visual Studio components by hand, then run this again:
    echo         clang++ / cmake / ninja  -  Visual Studio Installer, Modify, Individual
    echo                                     components: "C++ Clang tools for Windows"
    echo                                     and "C++ CMake tools for Windows"
)
if defined MISSING_GIT (
    echo       Install Git externally ^(not via Visual Studio C++ components^):
    echo         git                      -  winget install --id Git.Git -e --source winget
    echo                                     or https://git-scm.com/download/win
)
goto :fail

:nogame
echo   Extract default.xbe from your original Xbox disc into:
echo     %CD%\assets
echo   Keep the original ISO in assets for playback.
goto :fail

:fail
echo.
echo   BUILD FAILED. Read the last message above.
echo.
if not defined CI pause
exit /b 1
