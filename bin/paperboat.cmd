@echo off
rem The paperboat CLI from a Windows checkout: `bin\paperboat` in cmd or PowerShell (bin/paperboat is the
rem bash launcher). Runs the Visual Studio build's paperboat.exe, building it first if there is none.
setlocal
set "ENGINE=%~dp0..\engine"
for %%c in (Release RelWithDebInfo Debug) do (
  if exist "%ENGINE%\build\tools\%%c\paperboat.exe" (
    set "CLI=%ENGINE%\build\tools\%%c\paperboat.exe"
    goto run
  )
)
if exist "%ENGINE%\build\tools\paperboat.exe" (
  set "CLI=%ENGINE%\build\tools\paperboat.exe"
  goto run
)
where cmake >nul 2>nul || (
  echo paperboat: not built yet, and no cmake on PATH to build it. Open a "Developer PowerShell for VS" and run:>&2
  echo   cmake -S engine -B engine\build -G "Visual Studio 17 2022" -A x64 ^&^& cmake --build engine\build --config Release>&2
  exit /b 1
)
if not exist "%ENGINE%\build\CMakeCache.txt" cmake -S "%ENGINE%" -B "%ENGINE%\build" -A x64 >nul || exit /b 1
cmake --build "%ENGINE%\build" --config Release --target paperboat >nul || exit /b 1
set "CLI=%ENGINE%\build\tools\Release\paperboat.exe"
:run
"%CLI%" %*
exit /b %ERRORLEVEL%
