@echo off
REM Builds into build\: dinput8.dll (the permanent loader: loader, crashlog, padhook) and re5_solo_core.dll
REM (the mod; the loader reloads it while the game runs whenever the deployed file changes).
REM   build.bat          - build
REM   build.bat deploy   - build, then copy both next to re5dx9.exe: the core reloads in a running game
REM                        within a second; a changed loader takes effect at the game's next start

setlocal

set "HERE=%~dp0"
set "OUT_DIR=%HERE%build"
set "GAME=D:\Gaming\Launchers\Steam\steamapps\common\Resident Evil 5"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] ERROR: vswhere.exe not found - is Visual Studio Build Tools installed?
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)
if not defined VS_PATH (
    echo [build] ERROR: no VS install with the C++ toolchain found.
    exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
if errorlevel 1 (
    echo [build] ERROR: failed to initialise the x86 build environment.
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
pushd "%OUT_DIR%"
set "CL_FLAGS=/nologo /std:c++20 /EHa /O2 /MT /LD /W4 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /Brepro"
set "S=%HERE%src\"
cl %CL_FLAGS% "%S%loader.cpp" "%S%crashlog.cpp" "%S%padhook.cpp" /Fe:"dinput8.dll" /link /Brepro /DEF:"%S%dinput8.def" user32.lib dbghelp.lib
if errorlevel 1 goto :failed
cl %CL_FLAGS% "%S%core.cpp" "%S%solo.cpp" "%S%boat.cpp" "%S%objhook.cpp" "%S%gap.cpp" "%S%record.cpp" "%S%elevator.cpp" "%S%crank.cpp" "%S%box.cpp" "%S%croc.cpp" "%S%debug.cpp" "%S%lever.cpp" "%S%lift.cpp" "%S%jeep.cpp" "%S%ndesu.cpp" "%S%enemyhp.cpp" "%S%wesker.cpp" "%S%xinput.cpp" "%S%dinput.cpp" "%S%hint.cpp" "%S%sheva.cpp" "%S%script.cpp" "%S%zones.cpp" "%S%split.cpp" "%S%twist.cpp" "%S%collapse.cpp" "%S%pulley.cpp" "%S%cage.cpp" "%S%voice.cpp" "%S%filter.cpp" "%S%uroboros.cpp" "%S%backup.cpp" "%S%inventory.cpp" "%S%death.cpp" "%S%localjoin.cpp" "%S%snag.cpp" /Fe:"re5_solo_core.dll" /link /Brepro user32.lib advapi32.lib
if errorlevel 1 goto :failed
REM The launcher carries both DLLs (launcher.rc), so it is the one file to hand out.
rc /nologo /i "%OUT_DIR%" /fo launcher.res "%S%launcher.rc"
if errorlevel 1 goto :failed
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /DNOMINMAX /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Brepro "%S%launcher.cpp" "%S%scenefiles.cpp" launcher.res /Fe:"RE5 Solo Launcher.exe" /link /Brepro /SUBSYSTEM:WINDOWS /MANIFEST:EMBED user32.lib gdi32.lib shell32.lib ole32.lib comctl32.lib advapi32.lib dwmapi.lib
if errorlevel 1 goto :failed
popd
echo [build] SUCCESS  %OUT_DIR%\dinput8.dll, re5_solo_core.dll, RE5 Solo Launcher.exe

if /i not "%~1"=="deploy" goto :eof
REM A running game holds the loader open: overwriting fails, renaming works. Skip it when unchanged.
del /q "%GAME%\dinput8.dll.old*" >nul 2>&1
fc /b "%OUT_DIR%\dinput8.dll" "%GAME%\dinput8.dll" >nul 2>&1
if not errorlevel 1 (
    echo   loader unchanged
) else (
    copy /Y "%OUT_DIR%\dinput8.dll" "%GAME%\" >nul 2>&1
    if errorlevel 1 (
        ren "%GAME%\dinput8.dll" "dinput8.dll.old%RANDOM%"
        copy /Y "%OUT_DIR%\dinput8.dll" "%GAME%\" >nul
    )
    echo   deployed dinput8.dll - the loader changed: takes effect at the game's next start
)
REM The loader runs a copy of the core, so the file itself is never locked.
copy /Y "%OUT_DIR%\re5_solo_core.dll" "%GAME%\" >nul
if errorlevel 1 ( echo [build] WARNING: could not copy the core ) else ( echo   deployed re5_solo_core.dll - a running game reloads it )
goto :eof

:failed
popd
echo [build] FAILED
exit /b 1
