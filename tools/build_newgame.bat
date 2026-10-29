@echo off
rem Builds "RE5 New Game.exe": one file, static CRT, nothing to install.
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_PATH=%%i"
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
pushd "%HERE%"
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /DNOMINMAX /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS re5_newgame.cpp /Fe:"RE5 New Game.exe" /link advapi32.lib || (popd & exit /b 1)
del re5_newgame.obj
popd
echo [build] SUCCESS  %HERE%RE5 New Game.exe
