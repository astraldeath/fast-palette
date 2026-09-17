param([ValidateSet('Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
if (!(Test-Path -LiteralPath (Join-Path $taskRoot 'build/deps/qt-static/lib/cmake/Qt6/Qt6Config.cmake'))) {
    & (Join-Path $taskRoot 'tools/build-qt.ps1')
}
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $taskVswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVs) { throw 'MSVC C++ Build Tools were not found.' }
$taskDevCmd = Join-Path $taskVs 'Common7/Tools/VsDevCmd.bat'
$taskEnvironment = & cmd.exe /d /s /c "`"$taskDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the MSVC environment.' }
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
cmake -S $taskRoot -B (Join-Path $taskRoot 'build') -G 'Ninja Multi-Config'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build (Join-Path $taskRoot 'build') --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir (Join-Path $taskRoot 'build') -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
