@echo off
setlocal

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
set PROJECT=%~dp0FileCopyUtility.vcxproj

:: Default to Release|x64; pass CONFIG=Debug or PLATFORM=Win32 to override
set CONFIG=%1
set PLAT=%2
if "%CONFIG%"=="" set CONFIG=Release
if "%PLAT%"==""   set PLAT=x64

echo.
echo ========================================
echo  Building FileCopyUtility
echo  Configuration : %CONFIG%
echo  Platform      : %PLAT%
echo ========================================
echo.

:: MSYS_NO_PATHCONV=1 prevents Git Bash from mangling /p: flags as paths
set MSYS_NO_PATHCONV=1
%MSBUILD% "%PROJECT%" /p:Configuration=%CONFIG% /p:Platform=%PLAT% /m /nologo /v:minimal

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  BUILD FAILED  (exit code %ERRORLEVEL%)
    exit /b %ERRORLEVEL%
)

echo.
echo  Build succeeded.
echo  Output: %~dp0bin\%CONFIG%_%PLAT%\FileCopyUtility.exe
echo.
endlocal
