@echo off
rem ReCoF test launch: the arguments CoFLaunchApp gives cof.exe, plus -condebug, which writes the
rem console to qconsole.log next to cof.exe. Steam must be running. Change the settings below, then
rem double-click this file.

rem Screen size: leave both empty to keep the last one, or set for example 1920 and 1080
set WIDTH=
set HEIGHT=

rem 1 = windowed, 0 = fullscreen
set WINDOWED=0

rem More launch options, for example -novid (skip the intro videos), -rc_aff (pin to the first
rem core as retail did) or -rc_nosleep (the retail frame limiter)
set EXTRA=

cd /d "%~dp0"
set ARGS=-basedir cryoffear -game cryoffear -hw -heapsize 1536000 -no3dmenu -condebug
if "%WINDOWED%"=="1" (set ARGS=%ARGS% -windowed) else (set ARGS=%ARGS% -fullscreen)
if not "%WIDTH%"=="" set ARGS=%ARGS% -w %WIDTH% -h %HEIGHT%
start "" cof.exe %ARGS% %EXTRA%
