@echo off
setlocal
rem DeskNoise build (MSVC, single exe, static CRT)
rem   build.bat         -> release, build\DeskNoise.exe
rem   build.bat debug   -> debug,   build\DeskNoise-debug.exe

set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [ERROR] vcvars64.bat not found
  exit /b 1
)

set "ROOT=%~dp0"
set "OUT=%ROOT%build"
if not exist "%OUT%" mkdir "%OUT%"

set "EXE=DeskNoise.exe"
set "CFLAGS=/O2 /GL /MT /DNDEBUG"
set "LFLAGS=/LTCG"
if /i "%~1"=="debug" (
  set "EXE=DeskNoise-debug.exe"
  set "CFLAGS=/Od /Zi /MTd"
  set "LFLAGS=/DEBUG"
)

pushd "%OUT%"

rc /nologo /fo app.res "%ROOT%src\app.rc"
if errorlevel 1 goto fail

cl /nologo /utf-8 /W4 /EHsc /DUNICODE /D_UNICODE %CFLAGS% ^
   /Fe:%EXE% ^
   "%ROOT%src\main.cpp" "%ROOT%src\audio.cpp" "%ROOT%src\i18n.cpp" app.res ^
   /link %LFLAGS% /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /INCREMENTAL:NO
if errorlevel 1 goto fail

erase /q *.obj >nul 2>&1
popd
echo.
echo [OK] %OUT%\%EXE%
exit /b 0

:fail
popd
echo.
echo [ERROR] build failed
exit /b 1
