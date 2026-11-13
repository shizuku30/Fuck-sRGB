param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot 'Fuck-sRGB.exe'),
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$taskName = 'Fuck-sRGB Gamma2.2'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    # 使用当前 PowerShell 宿主，保留其脚本策略，不切换到可能禁用脚本的旧版宿主。
    $powershell = Join-Path $PSHOME 'pwsh.exe'
    if (!(Test-Path -LiteralPath $powershell)) { $powershell = Join-Path $PSHOME 'powershell.exe' }
    if (!(Test-Path -LiteralPath $powershell)) { throw 'A PowerShell console host was not found.' }
    $arguments = @('-NoProfile')
    $processPolicy = Get-ExecutionPolicy -Scope Process
    if ($processPolicy -ne 'Undefined') {
        $arguments += @('-ExecutionPolicy', [string]$processPolicy)
    }
    $arguments += @('-File', ('"' + $PSCommandPath + '"'))
    if ($Remove) { $arguments += '-Remove' }
    else {
        $source = (Get-Item -LiteralPath $ExecutablePath).FullName
        $arguments += @('-ExecutablePath', ('"' + $source + '"'))
    }
    $child = Start-Process -FilePath $powershell -ArgumentList $arguments -Verb RunAs `
        -WindowStyle Hidden -Wait -PassThru
    if ($child.ExitCode -ne 0) { throw "Startup installation failed (exit $($child.ExitCode))." }
    return
}

$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing) {
    $owner = $existing.Principal.UserId
    if (!$owner.StartsWith('S-1-')) {
        $owner = ([Security.Principal.NTAccount]::new($owner)).Translate([Security.Principal.SecurityIdentifier]).Value
    }
    if ($owner -ne $identity.User.Value) { throw 'The existing task belongs to another user.' }
}
if ($Remove) {
    if ($existing) { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false }
    return
}

$source = Get-Item -LiteralPath $ExecutablePath
if ($source.Extension -ne '.exe') { throw 'ExecutablePath must point to the compiled EXE.' }
$install = Join-Path $env:ProgramFiles 'Fuck-sRGB'
if (Test-Path -LiteralPath $install) {
    if ((Get-Item -LiteralPath $install).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'The installation directory must not be a link or junction.'
    }
} else { New-Item -ItemType Directory -Path $install | Out-Null }
$target = Join-Path $install 'Fuck-sRGB.exe'
if ((Test-Path -LiteralPath $target) -and
    ((Get-Item -LiteralPath $target).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'The installed executable must not be a link.'
}

# 最高权限的任务直接运行 Program Files 中的 EXE，不运行用户可写的启动脚本。
$oldPortable = Join-Path $env:LOCALAPPDATA 'Fuck-sRGB\Fuck-sRGB-Windows.exe'
$session = [Diagnostics.Process]::GetCurrentProcess().SessionId
foreach ($process in @(Get-Process -Name 'Fuck-sRGB', 'Fuck-sRGB-Windows' -ErrorAction SilentlyContinue)) {
    if ($process.SessionId -eq $session -and $process.Path -in @($target, $oldPortable)) {
        Stop-Process -InputObject $process
    }
}
if ($source.FullName -ne $target) { Copy-Item -LiteralPath $source.FullName -Destination $target -Force }
if ((Get-FileHash -LiteralPath $source.FullName).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
    throw 'Installed executable hash verification failed.'
}

$action = New-ScheduledTaskAction -Execute $target -Argument '--gamma22 --quiet' -WorkingDirectory $install
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $identity.User.Value
$trigger.Delay = 'PT15S'
$runAs = New-ScheduledTaskPrincipal -UserId $identity.User.Value -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $runAs `
    -Settings $settings -Description 'DWM-only gamma 2.2 while an active display uses HDR.' -Force | Out-Null
Start-ScheduledTask -TaskName $taskName

[pscustomobject]@{
    TaskName = $taskName
    Executable = $target
    Arguments = '--gamma22 --quiet'
    RunLevel = 'Highest'
    LogonType = 'Interactive'
    Delay = 'PT15S'
    SHA256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $install 'startup-install.json') -Encoding UTF8
