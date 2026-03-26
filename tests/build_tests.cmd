@echo off
setlocal
call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat" 2>/dev/null
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /EHsc /W3 /DUNICODE /D_UNICODE /I.. test_main.cpp /Fe:tests.exe shlwapi.lib
if %errorlevel%==0 (
    echo.
    tests.exe
) else (
    echo Build failed.
)
