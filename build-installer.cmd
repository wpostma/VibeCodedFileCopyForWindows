@echo off
setlocal

set ROOT=%~dp0
set BUILD_SCRIPT=%ROOT%build.cmd
set ISS_SCRIPT=%ROOT%installer\FileCopyUtility.iss
set ISCC_EXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe

echo.
echo ========================================
echo  Building FileCopyUtility installer
echo ========================================
echo.

if not exist "%BUILD_SCRIPT%" (
    echo ERROR: build.cmd not found at "%BUILD_SCRIPT%"
    exit /b 1
)

if not exist "%ISS_SCRIPT%" (
    echo ERROR: Inno Setup script not found at "%ISS_SCRIPT%"
    exit /b 1
)

if not exist "%ISCC_EXE%" (
    echo ERROR: Inno Setup compiler not found.
    echo Install Inno Setup 6 from https://jrsoftware.org/isdl.php
    exit /b 1
)

call "%BUILD_SCRIPT%" Release x64
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Installer build stopped because app build failed.
    exit /b %ERRORLEVEL%
)

echo.
echo Compiling installer...
"%ISCC_EXE%" "%ISS_SCRIPT%"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Installer compile failed.
    exit /b %ERRORLEVEL%
)

echo.
echo Installer created under "%ROOT%installers"
echo.
endlocal
