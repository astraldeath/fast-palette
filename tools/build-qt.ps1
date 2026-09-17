param(
    [ValidateRange(1, 8)][int]$Jobs = 6,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskDeps = Join-Path $taskRoot 'build/deps'
$taskSource = Join-Path $taskDeps 'qtbase'
$taskBuild = Join-Path $taskDeps 'qt-build'
$taskPrefix = Join-Path $taskDeps 'qt-static'
$taskRevision = 'ef55f427f2c8b410d34f8a7681020a3000cf6866'
$taskRepository = 'https://github.com/qt/qtbase.git'
$taskTag = 'v6.11.2'
$taskLogs = Join-Path $taskDeps 'qt-logs'
[void](New-Item -ItemType Directory -Force -Path $taskDeps, $taskLogs)
$taskLog = Join-Path $taskLogs ((Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')

function Invoke-QtBuildCommand {
    param([string]$Program, [string[]]$Arguments)
    ('> ' + $Program + ' ' + ($Arguments -join ' ')) | Tee-Object -FilePath $taskLog -Append
    & $Program @Arguments 2>&1 | Tee-Object -FilePath $taskLog -Append
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE. See $taskLog" }
}

$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $taskVswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVs) { throw 'MSVC C++ Build Tools were not found.' }
$taskDevCmd = Join-Path $taskVs 'Common7/Tools/VsDevCmd.bat'
$taskEnvironment = & cmd.exe /d /s /c "`"$taskDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x64 MSVC environment.' }
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

if (!(Test-Path -LiteralPath $taskSource)) {
    Invoke-QtBuildCommand git @('clone', '--depth', '1', '--branch', $taskTag, $taskRepository, $taskSource)
}
$taskActualRevision = & git -C $taskSource rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $taskActualRevision -ne $taskRevision) {
    throw "Qt source must be at $taskRevision ($taskTag); found $taskActualRevision. Existing source was left untouched."
}
& git -C $taskSource diff --quiet HEAD --
if ($LASTEXITCODE -ne 0) { throw 'Qt source contains tracked modifications. Preserve them separately before reproducing this pinned build.' }

# FEATURE_* are user inputs; QT_FEATURE_* are derived cache entries. Recompute
# derived entries without Qt's auto-reset overriding this explicit static preset.
$taskArguments = @(
    '-S', $taskSource, '-B', $taskBuild, '-G', 'Ninja', '-UQT_FEATURE_*', '-DQT_NO_FEATURE_AUTO_RESET=ON',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=$taskPrefix",
    '-DBUILD_SHARED_LIBS=OFF', '-DFEATURE_static_runtime=ON',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    '-DQT_BUILD_TESTS=OFF', '-DQT_BUILD_EXAMPLES=OFF', '-DQT_BUILD_DOCS=OFF',
    '-DFEATURE_windeployqt=OFF', '-DFEATURE_androiddeployqt=OFF',
    '-DFEATURE_gui=ON', '-DFEATURE_widgets=ON', '-DFEATURE_testlib=ON',
    '-DFEATURE_network=OFF', '-DFEATURE_sql=OFF', '-DFEATURE_dbus=OFF',
    '-DFEATURE_printsupport=OFF', '-DINPUT_opengl=no', '-DFEATURE_opengl=OFF', '-DFEATURE_opengl_dynamic=OFF', '-DFEATURE_vulkan=OFF',
    '-DFEATURE_xml=OFF', '-DFEATURE_concurrent=OFF', '-DFEATURE_icu=OFF',
    '-DFEATURE_openssl=OFF', '-DFEATURE_system_zlib=OFF', '-DFEATURE_system_pcre2=OFF',
    '-DFEATURE_system_png=OFF', '-DFEATURE_system_jpeg=OFF',
    '-DFEATURE_system_freetype=OFF', '-DFEATURE_system_harfbuzz=OFF'
)
@{
    qtVersion = '6.11.2'; sourceRepository = $taskRepository; sourceRevision = $taskRevision
    sourcePath = $taskSource; buildPath = $taskBuild; installPrefix = $taskPrefix
    visualStudioPath = $taskVs; configureArguments = $taskArguments; jobs = $Jobs
    logPath = $taskLog
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskLogs 'build-manifest.json') -Encoding utf8

Invoke-QtBuildCommand cmake $taskArguments
$taskCache = Get-Content -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt')
foreach ($taskFeature in @('static_runtime', 'gui', 'widgets', 'testlib')) {
    if (!($taskCache -match "^QT_FEATURE_${taskFeature}:INTERNAL=ON$")) {
        throw "Qt configuration did not retain required feature $taskFeature. See $taskLog"
    }
}
foreach ($taskFeature in @('shared', 'network', 'sql', 'dbus', 'printsupport', 'opengl', 'vulkan')) {
    if (!($taskCache -match "^QT_FEATURE_${taskFeature}:INTERNAL=OFF$")) {
        throw "Qt configuration unexpectedly enabled $taskFeature. See $taskLog"
    }
}
if ($ConfigureOnly) { Write-Output "Configured Qt. Build log: $taskLog"; return }
Invoke-QtBuildCommand cmake @('--build', $taskBuild, '--parallel', "$Jobs")
Invoke-QtBuildCommand cmake @('--install', $taskBuild)

foreach ($taskRequired in @('lib/cmake/Qt6/Qt6Config.cmake', 'lib/Qt6Core.lib', 'lib/Qt6Gui.lib', 'lib/Qt6Widgets.lib', 'lib/Qt6Test.lib', 'plugins/platforms/qwindows.lib')) {
    if (!(Test-Path -LiteralPath (Join-Path $taskPrefix $taskRequired))) { throw "Static Qt install is missing $taskRequired" }
}
$taskDlls = @(Get-ChildItem -LiteralPath $taskPrefix -Filter '*.dll' -File -Recurse)
if ($taskDlls.Count) { throw "Unexpected DLLs in the static Qt prefix: $($taskDlls.FullName -join ', ')" }

Write-Output "Static Qt installed. Qt6_DIR=$taskPrefix/lib/cmake/Qt6"
Write-Output "Pinned source, licenses, object files, compile commands, and logs are retained under $taskDeps."
Write-Output "Build log: $taskLog"
