@echo off
setlocal EnableDelayedExpansion
set ROOT=%~dp0
set KERN=%ROOT%kern
set CFG=%ROOT%config

if defined DSEE_GCC (
  set "GCC=%DSEE_GCC%"
  if not exist "%GCC%" (
    echo DSEE_GCC path does not exist: %GCC%
    exit /b 1
  )
) else (
  where gcc >nul 2>&1
  if errorlevel 1 (
    echo gcc not found. Install MinGW-w64 and add to PATH, or set DSEE_GCC to gcc.exe
    exit /b 1
  )
  set "GCC=gcc"
)

if not exist "%ROOT%build" mkdir "%ROOT%build"

set SRCS=
for /r "%KERN%" %%F in (*.c) do set SRCS=!SRCS! "%%F"
for /r "%CFG%" %%F in (*.c) do set SRCS=!SRCS! "%%F"

"%GCC%" -O3 -std=c11 -ffp-contract=off -Wall -Wextra -shared -static-libgcc ^
  -DDSEE_BUILDING=1 ^
  -I "%KERN%\include" ^
  -I "%KERN%\common" ^
  -I "%KERN%\common\ext80" ^
  -I "%KERN%\open\codebook" ^
  -I "%KERN%\dsx\band" ^
  -I "%KERN%\dsx\upsample" ^
  -I "%CFG%\include" ^
  -I "%CFG%\probe" ^
  -o "%ROOT%build\dsee_kern.dll" ^
  !SRCS! ^
  -lm
if errorlevel 1 exit /b 1
echo built %ROOT%build\dsee_kern.dll
