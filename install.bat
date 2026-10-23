@echo off
setlocal EnableExtensions

rem ---------------------------------------------------------------------
rem  install.bat - install the staged tree from build\install into
rem                C:\Program Files\zsh
rem
rem  Run it by double-clicking or from a terminal.  Administrator rights
rem  are requested automatically, because Program Files is protected.
rem ---------------------------------------------------------------------

set "SRC=%~dp0build\install"
if defined ProgramW6432 (
    set "DST=%ProgramW6432%\zsh"
) else (
    set "DST=%ProgramFiles%\zsh"
)

rem --- Check administrator rights ---------------------------------------
powershell -NoProfile -Command "if (([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { exit 0 } else { exit 1 }"
if errorlevel 1 (
    if defined WINZSH_ELEVATED (
        echo ERROR: administrator rights are still missing.
        echo Right-click install.bat and choose "Run as administrator".
        goto :fail
    )
    echo Administrator rights are required to write to "%DST%".
    echo Asking for elevation...
    set "WINZSH_ELEVATED=1"
    powershell -NoProfile -Command "try { Start-Process -FilePath '%~f0' -Verb RunAs -ErrorAction Stop } catch { exit 1 }"
    if errorlevel 1 (
        echo.
        echo Elevation was cancelled or failed.
        goto :fail
    )
    exit /b 0
)

rem --- The staged tree must exist ---------------------------------------
if not exist "%SRC%\bin\zsh.exe" (
    echo ERROR: "%SRC%\bin\zsh.exe" not found.
    echo Run "./build.sh --install" first: that stages build\install.
    goto :fail
)

for %%F in ("%SRC%\bin\zsh.exe") do (
    echo Source: %%~fF
    echo         %%~tF, %%~zF bytes
)

rem --- Create the destination if needed ---------------------------------
if not exist "%DST%" (
    echo Creating "%DST%" ...
    mkdir "%DST%"
    if errorlevel 1 (
        echo ERROR: could not create "%DST%".
        goto :fail
    )
)

rem --- Copy bin\ and share\ ---------------------------------------------
echo Copying to "%DST%" ...
xcopy "%SRC%\*" "%DST%\" /E /I /Y /Q >nul
set "RC=%ERRORLEVEL%"
if %RC% NEQ 0 (
    echo ERROR: xcopy returned %RC%.
    echo If zsh.exe is running in another window, close it and try again.
    goto :fail
)

if not exist "%DST%\bin\zsh.exe" (
    echo ERROR: "%DST%\bin\zsh.exe" is missing after the copy.
    goto :fail
)
if not exist "%DST%\share\zsh\functions" (
    echo ERROR: "%DST%\share\zsh\functions" is missing after the copy.
    goto :fail
)

rem The source tree may not carry this empty directory (git cannot
rem store empty directories), but zsh looks for it in fpath.
if not exist "%DST%\share\zsh\site-functions" (
    mkdir "%DST%\share\zsh\site-functions" >nul 2>&1
)

echo.
echo Installed:
echo   shell    %DST%\bin\zsh.exe
echo   library  %DST%\share\zsh\functions
echo.
echo If "%DST%\bin" is not on PATH, add it - see INSTALLATION.txt.
goto :done

:fail
echo.
echo Installation failed.
set "RC=1"
goto :maybe_pause

:done
set "RC=0"

:maybe_pause
rem Keep the window open when this file was double-clicked.
echo %cmdcmdline% | find /i "%~nx0" >nul
if not errorlevel 1 pause
exit /b %RC%
