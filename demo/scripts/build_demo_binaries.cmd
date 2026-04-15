@echo off
setlocal

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo vcvars64.bat not found: %VCVARS%
  exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

if not exist "%ROOT%\build-demo" mkdir "%ROOT%\build-demo"

pushd "%ROOT%"
del /q *.obj 2>nul

cl.exe /nologo /utf-8 /std:c11 /I include /Fe:build-demo\sql_processor_trace.exe ^
  src\main_trace.c src\sql_trace.c src\lexer.c src\parser.c src\ast.c src\csv_storage.c src\executor.c src\week7\bplus_tree.c src\week7\week7_index.c
if errorlevel 1 goto :fail

cl.exe /nologo /utf-8 /std:c11 /I include /Fe:build-demo\bench_bplus.exe ^
  src\bench_bplus.c src\week7\bplus_tree.c
if errorlevel 1 goto :fail

cl.exe /nologo /utf-8 /std:c11 /I include /DBP_MAX_KEYS=31 /Fe:build-demo\bench_bplus_wide.exe ^
  src\bench_bplus.c src\week7\bplus_tree.c
if errorlevel 1 goto :fail

echo build-demo binaries ready
del /q *.obj 2>nul
popd
exit /b 0

:fail
del /q *.obj 2>nul
popd
exit /b 1
