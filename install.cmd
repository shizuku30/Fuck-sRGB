@echo off
setlocal
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0install-startup.ps1" -ExecutablePath "%~dp0Fuck-sRGB.exe"
set "result=%errorlevel%"
if not "%result%"=="0" (
  echo Installation failed. See DEPLOY.zh-CN.txt for script policy and manual setup instructions.
  pause
)
exit /b %result%
