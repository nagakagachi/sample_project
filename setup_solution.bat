@echo off
setlocal

set ROOT=%~dp0
set DEPS_SOURCE=%ROOT%ngl_v001\deps
set DEPS_BUILD=%ROOT%ngl_v001\external_build
set SLN_PATH=%ROOT%ngl_v001\ngl_v001.sln

if not exist "%DEPS_SOURCE%\CMakeLists.txt" (
  echo Missing %DEPS_SOURCE%\CMakeLists.txt
  exit /b 1
)

git submodule update --init --recursive
if errorlevel 1 exit /b 1

msbuild "%SLN_PATH%" /t:Restore /p:RestorePackagesConfig=true
if errorlevel 1 exit /b 1

cmake -S "%DEPS_SOURCE%" -B "%DEPS_BUILD%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1

cmake --build "%DEPS_BUILD%" --config Debug --target deps
if errorlevel 1 exit /b 1

cmake --build "%DEPS_BUILD%" --config Release --target deps
if errorlevel 1 exit /b 1

endlocal
