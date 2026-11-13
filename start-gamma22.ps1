$ErrorActionPreference = 'Stop'
$taskName = 'Fuck-sRGB Gamma2.2'
$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task) {
    Start-ScheduledTask -TaskName $taskName
} else {
    $exe = Join-Path $env:ProgramFiles 'Fuck-sRGB\Fuck-sRGB.exe'
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing executable: $exe" }
    Start-Process -FilePath $exe -ArgumentList '--gamma22', '--quiet' -Verb RunAs -WindowStyle Hidden
}
