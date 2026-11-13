@echo off
setlocal
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0install-startup.ps1" -Remove
set "result=%errorlevel%"
if not "%result%"=="0" (
  echo Removing startup failed. See DEPLOY.zh-CN.txt for instructions.
  pause
)
exit /b %result%
