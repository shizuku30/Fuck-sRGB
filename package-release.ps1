param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'))

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$work = Join-Path $output ([Guid]::NewGuid().ToString('N'))
$build = Join-Path $work 'build'
$payload = Join-Path $work 'payload'
New-Item -ItemType Directory -Path $build, $payload -Force | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ build tools and a recent Windows SDK.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'An x64 MSVC toolchain was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$variables = @{
    SRGB_PACKAGE_ROOT = $root
    SRGB_PACKAGE_BUILD = $build
    SRGB_PACKAGE_PAYLOAD = $payload
    SRGB_PACKAGE_VCVARS = $vcvars
}
$previous = @{}
foreach ($name in $variables.Keys) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $variables[$name], 'Process')
}
$commands = @'
@call "%SRGB_PACKAGE_VCVARS%" >nul
@if errorlevel 1 exit /b %errorlevel%
@rc /nologo /fo "%SRGB_PACKAGE_BUILD%\sdr-gamma.res" "%SRGB_PACKAGE_ROOT%\sdr-gamma.rc"
@if errorlevel 1 exit /b %errorlevel%
@cl /nologo /std:c++17 /W4 /WX /O2 /utf-8 /EHsc /MT /Fo"%SRGB_PACKAGE_BUILD%\tray.obj" /Fe"%SRGB_PACKAGE_PAYLOAD%\Fuck-sRGB.exe" "%SRGB_PACKAGE_ROOT%\sdr-gamma-tray.cpp" "%SRGB_PACKAGE_BUILD%\sdr-gamma.res" /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" bcrypt.lib advapi32.lib user32.lib shell32.lib
@if errorlevel 1 exit /b %errorlevel%
@cl /nologo /std:c++17 /W4 /WX /O2 /utf-8 /EHsc /MT /Fo"%SRGB_PACKAGE_BUILD%\policy.obj" /Fe"%SRGB_PACKAGE_BUILD%\hdr-policy.exe" "%SRGB_PACKAGE_ROOT%\tests\hdr-policy.cpp" /link bcrypt.lib advapi32.lib user32.lib shell32.lib
@if errorlevel 1 exit /b %errorlevel%
@"%SRGB_PACKAGE_BUILD%\hdr-policy.exe"
@exit /b %errorlevel%
'@
$batch = Join-Path $build 'build.cmd'
Set-Content -LiteralPath $batch -Value $commands -Encoding Ascii
Push-Location $root
try {
    & cmd.exe /d /c $batch
    if ($LASTEXITCODE -ne 0) { throw "Build or policy checks failed (exit $LASTEXITCODE)." }
} finally {
    Pop-Location
    foreach ($name in $variables.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}

$files = @('install-startup.ps1', 'start-gamma22.ps1', 'install.cmd', 'remove-startup.cmd', 'README.md', 'DEPLOY.zh-CN.txt', 'LICENSE')
foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $root $file) -Destination $payload }
Push-Location $root
try {
    $revision = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision.' }
    $dirty = @(& git status --porcelain)
    $source = "Repository: https://github.com/shizuku30/Fuck-sRGB`nCommit: $revision`nWorking tree changes: $($dirty.Count -ne 0)`n"
    Set-Content -LiteralPath (Join-Path $payload 'SOURCE.txt') -Value $source -Encoding Ascii
} finally { Pop-Location }

$hashes = Get-ChildItem -LiteralPath $payload -File | Sort-Object Name | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
}
Set-Content -LiteralPath (Join-Path $payload 'SHA256SUMS.txt') -Value $hashes -Encoding Ascii
$version = (Get-Item -LiteralPath (Join-Path $payload 'Fuck-sRGB.exe')).VersionInfo.ProductVersion
$name = "Fuck-sRGB-$version-win-x64"
$stage = Join-Path $work $name
Rename-Item -LiteralPath $payload -NewName $name
$zip = Join-Path $output "$name.zip"
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal -Force
$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$zip.sha256" -Value "$zipHash  $name.zip" -Encoding Ascii
Write-Output "Package: $zip"
Write-Output "SHA256: $zipHash"
