[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build',
    [string]$DistributionDirectory = 'dist'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskBuild = [IO.Path]::GetFullPath((Join-Path $taskRoot $BuildDirectory))
$taskDist = [IO.Path]::GetFullPath((Join-Path $taskRoot $DistributionDirectory))
$taskQtSource = Join-Path $taskBuild 'deps/qtbase'
$taskQtBuild = Join-Path $taskBuild 'deps/qt-build'
$taskQtPrefix = Join-Path $taskBuild 'deps/qt-static'
$taskExecutable = Join-Path $taskBuild "$Configuration/FastPalette.exe"
$taskPackageOutput = Join-Path $taskBuild 'package'
$taskRelinkKit = Join-Path $taskBuild 'relink-kit'
$taskRelinkArchive = Join-Path $taskBuild 'FastPalette-relink-kit.zip'
$taskRevision = 'ef55f427f2c8b410d34f8a7681020a3000cf6866'
$taskQtVersion = '6.11.2'
$taskStage = Join-Path $taskBuild ('.package-staging-' + [Guid]::NewGuid().ToString('N'))

function Assert-RepositoryChild {
    param([Parameter(Mandatory)][string]$Path)
    $taskResolved = [IO.Path]::GetFullPath($Path)
    $taskPrefix = $taskRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (!$taskResolved.StartsWith($taskPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the repository: $taskResolved"
    }
}

function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$Program,
        [Parameter(Mandatory)][string[]]$Arguments,
        [switch]$Capture
    )
    if ($Capture) {
        $taskOutput = @(& $Program @Arguments 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "$Program failed with exit code $LASTEXITCODE.`n$($taskOutput -join [Environment]::NewLine)"
        }
        return $taskOutput
    }
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Required packaging input is missing: $Source" }
    $taskParent = Split-Path -Parent $Destination
    if ($taskParent) { [void](New-Item -ItemType Directory -Force -Path $taskParent) }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Import-MsvcEnvironment {
    if (Get-Command dumpbin.exe -ErrorAction SilentlyContinue) { return }
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $taskVswhere -PathType Leaf)) {
        throw 'dumpbin.exe is unavailable and Visual Studio Installer could not be found.'
    }
    $taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$taskVs) { throw 'MSVC C++ Build Tools were not found.' }
    $taskDevCmd = Join-Path $taskVs 'Common7/Tools/VsDevCmd.bat'
    $taskEnvironment = & cmd.exe /d /s /c "`"$taskDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x64 MSVC environment.' }
    foreach ($taskLine in $taskEnvironment) {
        if ($taskLine -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    if (!(Get-Command dumpbin.exe -ErrorAction SilentlyContinue)) { throw 'dumpbin.exe remains unavailable after MSVC environment setup.' }
}

function Get-ImportedDlls {
    param([Parameter(Mandatory)][string]$Executable)
    $taskDump = @(Invoke-Native dumpbin.exe @('/NOLOGO', '/DEPENDENTS', $Executable) -Capture)
    $taskDlls = @(
        foreach ($taskLine in $taskDump) {
            if ($taskLine -match '^\s+([A-Za-z0-9][A-Za-z0-9_.-]*\.dll)\s*$') { $Matches[1] }
        }
    ) | Sort-Object -Unique
    if (!$taskDlls.Count) { throw 'dumpbin did not report any imported DLLs; dependency verification could not be completed.' }
    return [pscustomobject]@{ Dlls = $taskDlls; Output = $taskDump }
}

function Assert-SystemOnlyImports {
    param([Parameter(Mandatory)][string[]]$Dlls)
    $taskForbidden = @($Dlls | Where-Object { $_ -match '^(Qt\d|VCRUNTIME|MSVCP|CONCRT).*\.dll$' })
    if ($taskForbidden.Count) {
        throw "Portable build imports forbidden framework/runtime DLLs: $($taskForbidden -join ', ')"
    }
    $taskSystem32 = Join-Path $env:SystemRoot 'System32'
    $taskUnexpected = @(
        $Dlls | Where-Object {
            $_ -notmatch '^(api-ms-win-|ext-ms-win-)' -and
            !(Test-Path -LiteralPath (Join-Path $taskSystem32 $_) -PathType Leaf)
        }
    )
    if ($taskUnexpected.Count) {
        throw "Portable build imports DLLs that are not Windows system DLLs: $($taskUnexpected -join ', ')"
    }
}

function Get-SingleFile {
    param(
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][string]$Filter,
        [Parameter(Mandatory)][string]$Description
    )
    $taskMatches = @(Get-ChildItem -LiteralPath $Directory -Filter $Filter -File -Recurse -ErrorAction SilentlyContinue)
    if ($taskMatches.Count -ne 1) {
        throw "Expected one $Description under $Directory; found $($taskMatches.Count)."
    }
    return $taskMatches[0].FullName
}

function Test-FileLocked {
    param([Parameter(Mandatory)][string]$Path)
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        $taskStream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $taskStream.Dispose()
        return $false
    }
    catch [IO.IOException] {
        return $true
    }
}

function Assert-DistributionReplaceable {
    if (!(Test-Path -LiteralPath $taskDist)) { return }
    if (!(Test-Path -LiteralPath $taskDist -PathType Container)) {
        throw "Distribution path exists but is not a directory: $taskDist"
    }
    $taskExistingDistEntries = @(Get-ChildItem -LiteralPath $taskDist -Force)
    $taskUnexpectedDistEntries = @($taskExistingDistEntries | Where-Object { $_.PSIsContainer -or $_.Name -ne 'FastPalette.exe' })
    if ($taskUnexpectedDistEntries.Count) {
        throw "Distribution directory contains files not created by this packager; it was left untouched: $($taskUnexpectedDistEntries.FullName -join ', ')"
    }
}

function Write-RelinkProject {
    param([Parameter(Mandatory)][string]$Directory)

    @'
cmake_minimum_required(VERSION 3.24)
project(FastPaletteRelink LANGUAGES CXX RC)
if(NOT WIN32)
  message(FATAL_ERROR "Fast Palette is built exclusively for Windows.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets)
get_target_property(RELINK_QT_CORE_TYPE Qt6::Core TYPE)
if(NOT RELINK_QT_CORE_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "Relinking requires a static Qt build.")
endif()
foreach(app_library IN ITEMS palette_ui palette_platform palette_keyboard palette_core)
  add_library(${app_library} STATIC IMPORTED GLOBAL)
  set_target_properties(${app_library} PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/app-libs/${app_library}.lib")
endforeach()
set(relink_application_objects
  "${CMAKE_CURRENT_LIST_DIR}/app-objects/main.cpp.obj"
  "${CMAKE_CURRENT_LIST_DIR}/app-objects/qrc_licenses.cpp.obj"
  "${CMAKE_CURRENT_LIST_DIR}/app-objects/qrc_theme_assets.cpp.obj"
  "${CMAKE_CURRENT_LIST_DIR}/app-objects/qrc_theme_assets_init.cpp.obj"
  "${CMAKE_CURRENT_LIST_DIR}/app-objects/app.rc.res")
set_source_files_properties(${relink_application_objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
qt_add_executable(FastPalette WIN32
  ${relink_application_objects}
  "${CMAKE_CURRENT_LIST_DIR}/app-resources/app.manifest")
set_target_properties(FastPalette PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(FastPalette PRIVATE
  palette_ui palette_platform palette_keyboard palette_core
  Qt6::Widgets wtsapi32 shell32 ole32 propsys advapi32 user32 uuid)
qt_import_plugins(FastPalette INCLUDE Qt6::QWindowsIntegrationPlugin)
'@ | Set-Content -LiteralPath (Join-Path $Directory 'CMakeLists.txt') -Encoding utf8

    @'
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$QtPrefix,
    [string]$BuildDirectory = 'relink-build'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$kitRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$qtRoot = [IO.Path]::GetFullPath($QtPrefix)
$outputBuild = [IO.Path]::GetFullPath((Join-Path $kitRoot $BuildDirectory))
$qtConfig = Join-Path $qtRoot 'lib/cmake/Qt6/Qt6Config.cmake'
if (!(Test-Path -LiteralPath $qtConfig -PathType Leaf)) { throw "Static Qt prefix is missing Qt6Config.cmake: $qtConfig" }
if (@(Get-ChildItem -LiteralPath $qtRoot -Filter '*.dll' -File -Recurse).Count) { throw 'The replacement Qt prefix contains DLLs; use a static Qt /MT build.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'MSVC C++ Build Tools were not found.' }
$devCmd = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$environment = & cmd.exe /d /s /c "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x64 MSVC environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
& cmake -S $kitRoot -B $outputBuild -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qtRoot"
if ($LASTEXITCODE -ne 0) { throw 'Relink configuration failed.' }
& cmake --build $outputBuild
if ($LASTEXITCODE -ne 0) { throw 'Relink failed.' }
Write-Output "Relinked executable: $(Join-Path $outputBuild 'FastPalette.exe')"
'@ | Set-Content -LiteralPath (Join-Path $Directory 'relink.ps1') -Encoding utf8

    @'
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$QtSource,
    [Parameter(Mandatory)][string]$QtBuild,
    [Parameter(Mandatory)][string]$QtPrefix,
    [ValidateRange(1, 64)][int]$Jobs = 6
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$source = [IO.Path]::GetFullPath($QtSource)
$build = [IO.Path]::GetFullPath($QtBuild)
$prefix = [IO.Path]::GetFullPath($QtPrefix)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'MSVC C++ Build Tools were not found.' }
$devCmd = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$environment = & cmd.exe /d /s /c "`"$devCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x64 MSVC environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$arguments = @(
    '-S', $source, '-B', $build, '-G', 'Ninja', '-UQT_FEATURE_*', '-DQT_NO_FEATURE_AUTO_RESET=ON',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=$prefix",
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
& cmake @arguments
if ($LASTEXITCODE -ne 0) { throw 'Qt configuration failed.' }
& cmake --build $build --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Qt build failed.' }
& cmake --install $build
if ($LASTEXITCODE -ne 0) { throw 'Qt install failed.' }
if (@(Get-ChildItem -LiteralPath $prefix -Filter '*.dll' -File -Recurse).Count) { throw 'The rebuilt Qt prefix unexpectedly contains DLLs.' }
'@ | Set-Content -LiteralPath (Join-Path $Directory 'rebuild-qt.ps1') -Encoding utf8
}

Assert-RepositoryChild $taskBuild
Assert-RepositoryChild $taskDist
Assert-RepositoryChild $taskPackageOutput
Assert-RepositoryChild $taskRelinkKit
Assert-RepositoryChild $taskRelinkArchive
Assert-RepositoryChild $taskStage

foreach ($taskGeneratedOutput in @($taskPackageOutput, $taskRelinkKit, $taskRelinkArchive)) {
    if (Test-Path -LiteralPath $taskGeneratedOutput) {
        throw "Generated packaging output already exists; preserve or remove it explicitly before packaging again: $taskGeneratedOutput"
    }
}
Assert-DistributionReplaceable

if (Test-FileLocked (Join-Path $taskQtBuild '.ninja_lock')) {
    throw 'The pinned Qt build is still running. Wait for it to finish before packaging.'
}
foreach ($taskRequired in @(
    $taskExecutable,
    (Join-Path $taskQtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'),
    (Join-Path $taskRoot 'resources/THIRD_PARTY_NOTICES.txt')
)) {
    if (!(Test-Path -LiteralPath $taskRequired -PathType Leaf)) { throw "Required packaging input is missing: $taskRequired" }
}
$taskQtDlls = @(Get-ChildItem -LiteralPath $taskQtPrefix -Filter '*.dll' -File -Recurse)
if ($taskQtDlls.Count) { throw "Static Qt prefix contains unexpected DLLs: $($taskQtDlls.FullName -join ', ')" }
$taskQtCache = Join-Path $taskQtBuild 'CMakeCache.txt'
$taskQtCacheText = Get-Content -LiteralPath $taskQtCache
foreach ($taskExpectedFeature in @('QT_FEATURE_static_runtime:INTERNAL=ON', 'QT_FEATURE_shared:INTERNAL=OFF')) {
    if (!($taskQtCacheText -contains $taskExpectedFeature)) { throw "Qt build configuration is missing $taskExpectedFeature." }
}
$taskAppCache = Join-Path $taskBuild 'CMakeCache.txt'
$taskAppCacheText = Get-Content -LiteralPath $taskAppCache
if (!($taskAppCacheText -match '^CMAKE_MSVC_RUNTIME_LIBRARY:')) {
    # CMAKE_MSVC_RUNTIME_LIBRARY can be a project-level normal variable and therefore absent from the cache.
    $taskProjectText = Get-Content -LiteralPath (Join-Path $taskRoot 'CMakeLists.txt') -Raw
    if ($taskProjectText -notmatch 'CMAKE_MSVC_RUNTIME_LIBRARY\s+"MultiThreaded') {
        throw 'Application configuration does not require the static MSVC runtime.'
    }
}
$taskActualRevision = (Invoke-Native git @('-C', $taskQtSource, 'rev-parse', 'HEAD') -Capture | Select-Object -First 1).Trim()
if ($taskActualRevision -ne $taskRevision) { throw "Qt source revision is $taskActualRevision; expected $taskRevision." }
$taskQtChanges = @(Invoke-Native git @('-C', $taskQtSource, 'status', '--short', '--untracked-files=no') -Capture)
if ($taskQtChanges.Count) { throw 'Pinned Qt source contains tracked changes; preserve or revert them before packaging.' }

Import-MsvcEnvironment
$taskImports = Get-ImportedDlls $taskExecutable
Assert-SystemOnlyImports $taskImports.Dlls
$taskExeHash = (Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
$taskExeInfo = Get-Item -LiteralPath $taskExecutable

[void](New-Item -ItemType Directory -Force -Path $taskStage)
try {
    $taskKitStage = Join-Path $taskStage 'relink-kit'
    $taskDistStage = Join-Path $taskStage 'dist'
    [void](New-Item -ItemType Directory -Force -Path $taskKitStage, $taskDistStage)

    Copy-RequiredFile $taskExecutable (Join-Path $taskDistStage 'FastPalette.exe')

    $taskKitDirectories = @('app-libs', 'app-objects', 'app-resources', 'build-inputs/tools', 'build-inputs/generated/app', 'build-inputs/generated/qt', 'licenses', 'source')
    foreach ($taskDirectory in $taskKitDirectories) { [void](New-Item -ItemType Directory -Force -Path (Join-Path $taskKitStage $taskDirectory)) }

    foreach ($taskLibrary in @('palette_ui.lib', 'palette_platform.lib', 'palette_keyboard.lib', 'palette_core.lib')) {
        Copy-RequiredFile (Join-Path $taskBuild "$Configuration/$taskLibrary") (Join-Path $taskKitStage "app-libs/$taskLibrary")
    }
    $taskTargetObjects = Join-Path $taskBuild "CMakeFiles/FastPalette.dir/$Configuration"
    $taskMainObject = Get-SingleFile $taskTargetObjects 'main.cpp.obj' 'FastPalette main object'
    $taskLicenseResourceObject = Get-SingleFile $taskTargetObjects 'qrc_licenses.cpp.obj' 'embedded license resource object'
    $taskThemeResourceMatches = @(
        Get-ChildItem -LiteralPath (Join-Path $taskBuild 'CMakeFiles') -Filter 'qrc_theme_assets.cpp.obj' -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\$([regex]::Escape($Configuration))\\" }
    )
    if ($taskThemeResourceMatches.Count -ne 1) {
        throw "Expected one compiled theme resource object for $Configuration; found $($taskThemeResourceMatches.Count)."
    }
    $taskThemeResourceObject = $taskThemeResourceMatches[0].FullName
    $taskThemeInitMatches = @(
        Get-ChildItem -LiteralPath (Join-Path $taskBuild 'CMakeFiles') -Filter 'qrc_theme_assets_init.cpp.obj' -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\$([regex]::Escape($Configuration))\\" }
    )
    if ($taskThemeInitMatches.Count -ne 1) {
        throw "Expected one compiled theme resource initializer for $Configuration; found $($taskThemeInitMatches.Count)."
    }
    $taskThemeInitObject = $taskThemeInitMatches[0].FullName
    $taskResource = Get-SingleFile $taskTargetObjects '*.res' 'FastPalette resource object'
    $taskWindowsResourceInfo = Get-Item -LiteralPath $taskResource
    $taskWindowsResourceSourceInfo = Get-Item -LiteralPath (Join-Path $taskRoot 'resources/app.rc')
    $taskManifestInfo = Get-Item -LiteralPath (Join-Path $taskRoot 'resources/app.manifest')
    if ($taskWindowsResourceInfo.LastWriteTimeUtc -lt $taskWindowsResourceSourceInfo.LastWriteTimeUtc) {
        throw 'The compiled Windows resource is older than app.rc; rebuild FastPalette before packaging.'
    }
    if ($taskExeInfo.LastWriteTimeUtc -lt $taskWindowsResourceInfo.LastWriteTimeUtc -or
        $taskExeInfo.LastWriteTimeUtc -lt $taskManifestInfo.LastWriteTimeUtc) {
        throw 'FastPalette.exe is older than its Windows resource or manifest; rebuild FastPalette before packaging.'
    }
    $taskNoticeInfo = Get-Item -LiteralPath (Join-Path $taskRoot 'resources/THIRD_PARTY_NOTICES.txt')
    $taskLicenseObjectInfo = Get-Item -LiteralPath $taskLicenseResourceObject
    if ($taskLicenseObjectInfo.LastWriteTimeUtc -lt $taskNoticeInfo.LastWriteTimeUtc) {
        throw 'The embedded-license resource object is older than THIRD_PARTY_NOTICES.txt; rebuild FastPalette before packaging.'
    }
    if ($taskExeInfo.LastWriteTimeUtc -lt $taskLicenseObjectInfo.LastWriteTimeUtc) {
        throw 'FastPalette.exe is older than its embedded-license resource object; rebuild FastPalette before packaging.'
    }
    $taskThemeObjectInfo = Get-Item -LiteralPath $taskThemeResourceObject
    foreach ($taskThemeSource in @('resources/check-dark.png', 'resources/check-light.png')) {
        $taskThemeSourceInfo = Get-Item -LiteralPath (Join-Path $taskRoot $taskThemeSource)
        if ($taskThemeObjectInfo.LastWriteTimeUtc -lt $taskThemeSourceInfo.LastWriteTimeUtc) {
            throw "The compiled theme resource is older than $taskThemeSource; rebuild FastPalette before packaging."
        }
    }
    if ($taskExeInfo.LastWriteTimeUtc -lt $taskThemeObjectInfo.LastWriteTimeUtc) {
        throw 'FastPalette.exe is older than its compiled theme resource; rebuild FastPalette before packaging.'
    }
    $taskThemeInitInfo = Get-Item -LiteralPath $taskThemeInitObject
    if ($taskExeInfo.LastWriteTimeUtc -lt $taskThemeInitInfo.LastWriteTimeUtc) {
        throw 'FastPalette.exe is older than its theme resource initializer; rebuild FastPalette before packaging.'
    }
    Copy-RequiredFile $taskMainObject (Join-Path $taskKitStage 'app-objects/main.cpp.obj')
    Copy-RequiredFile $taskLicenseResourceObject (Join-Path $taskKitStage 'app-objects/qrc_licenses.cpp.obj')
    Copy-RequiredFile $taskThemeResourceObject (Join-Path $taskKitStage 'app-objects/qrc_theme_assets.cpp.obj')
    Copy-RequiredFile $taskThemeInitObject (Join-Path $taskKitStage 'app-objects/qrc_theme_assets_init.cpp.obj')
    Copy-RequiredFile $taskResource (Join-Path $taskKitStage 'app-objects/app.rc.res')
    Copy-RequiredFile (Join-Path $taskRoot 'resources/app.manifest') (Join-Path $taskKitStage 'app-resources/app.manifest')

    Copy-RequiredFile (Join-Path $taskRoot 'CMakeLists.txt') (Join-Path $taskKitStage 'build-inputs/CMakeLists.txt')
    Copy-RequiredFile (Join-Path $taskRoot 'build.ps1') (Join-Path $taskKitStage 'build-inputs/build.ps1')
    Copy-RequiredFile (Join-Path $taskRoot 'tools/build-qt.ps1') (Join-Path $taskKitStage 'build-inputs/tools/build-qt.ps1')
    Copy-RequiredFile (Join-Path $taskRoot 'tools/package.ps1') (Join-Path $taskKitStage 'build-inputs/tools/package.ps1')
    Copy-RequiredFile (Join-Path $taskRoot 'resources/THIRD_PARTY_NOTICES.txt') (Join-Path $taskKitStage 'THIRD_PARTY_NOTICES.txt')

    foreach ($taskGenerated in @('CMakeCache.txt', 'build.ninja', 'build-Release.ninja', 'cmake_install.cmake', 'compile_commands.json')) {
        $taskGeneratedSource = Join-Path $taskBuild $taskGenerated
        if (Test-Path -LiteralPath $taskGeneratedSource -PathType Leaf) {
            Copy-RequiredFile $taskGeneratedSource (Join-Path $taskKitStage "build-inputs/generated/app/$taskGenerated")
        }
    }
    foreach ($taskGenerated in @('CMakeCache.txt', 'config.summary', 'config.redo.bat', 'compile_commands.json')) {
        $taskGeneratedSource = Join-Path $taskQtBuild $taskGenerated
        if (Test-Path -LiteralPath $taskGeneratedSource -PathType Leaf) {
            Copy-RequiredFile $taskGeneratedSource (Join-Path $taskKitStage "build-inputs/generated/qt/$taskGenerated")
        }
    }
    $taskManifest = Join-Path $taskBuild 'deps/qt-logs/build-manifest.json'
    if (Test-Path -LiteralPath $taskManifest -PathType Leaf) {
        Copy-RequiredFile $taskManifest (Join-Path $taskKitStage 'build-inputs/generated/qt/build-manifest.json')
    }

    Copy-Item -LiteralPath (Join-Path $taskQtSource 'LICENSES') -Destination (Join-Path $taskKitStage 'licenses/qtbase-LICENSES') -Recurse -Force
    $taskAttributionRoot = Join-Path $taskKitStage 'licenses/qt-attributions'
    [void](New-Item -ItemType Directory -Force -Path $taskAttributionRoot)
    foreach ($taskComponent in @('double-conversion', 'emoji-segmenter', 'freetype', 'harfbuzz-ng', 'libjpeg', 'libpng', 'md4c', 'pcre2', 'zlib')) {
        $taskAttribution = Join-Path $taskQtSource "src/3rdparty/$taskComponent/qt_attribution.json"
        if (Test-Path -LiteralPath $taskAttribution -PathType Leaf) {
            Copy-RequiredFile $taskAttribution (Join-Path $taskAttributionRoot "$taskComponent.json")
        }
    }

    $taskSourceArchive = Join-Path $taskKitStage "source/qtbase-$taskQtVersion-$taskRevision.zip"
    Invoke-Native git @('-C', $taskQtSource, 'archive', '--format=zip', "--prefix=qtbase-$taskQtVersion/", '-o', $taskSourceArchive, $taskRevision)
    if (!(Test-Path -LiteralPath $taskSourceArchive -PathType Leaf)) { throw 'git archive did not create the Qt source archive.' }

    $taskMakeProgram = $null
    if (Test-Path -LiteralPath $taskAppCache -PathType Leaf) {
        $taskMakeLine = Select-String -LiteralPath $taskAppCache -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$' | Select-Object -First 1
        if ($taskMakeLine) { $taskMakeProgram = $taskMakeLine.Matches[0].Groups[1].Value }
    }
    if (!$taskMakeProgram -or !(Test-Path -LiteralPath $taskMakeProgram -PathType Leaf)) {
        $taskNinjaCommand = Get-Command ninja.exe -ErrorAction SilentlyContinue
        if ($taskNinjaCommand) { $taskMakeProgram = $taskNinjaCommand.Source }
    }
    if (!$taskMakeProgram) { throw 'Ninja was not found; the exact application build commands cannot be captured.' }
    $taskNinjaFile = if (Test-Path -LiteralPath (Join-Path $taskBuild 'build-Release.ninja')) { 'build-Release.ninja' } else { 'build.ninja' }
    $taskBuildCommands = @(Invoke-Native $taskMakeProgram @('-C', $taskBuild, '-f', $taskNinjaFile, '-t', 'commands', 'FastPalette') -Capture)
    $taskBuildCommands | Set-Content -LiteralPath (Join-Path $taskKitStage 'build-inputs/generated/app/FastPalette-build-commands.txt') -Encoding utf8

    Write-RelinkProject $taskKitStage
    @"
Fast Palette static Qt relink kit

This kit accompanies FastPalette.exe built with Qt Base $taskQtVersion at revision $taskRevision.
It is separate from the one-file runtime distribution.

Contents:
  source/                  Exact Qt Base source archive from the linked revision.
  licenses/                Complete Qt Base license set and bundled component attribution metadata.
  app-libs, app-objects/   Application material needed to relink without application source disclosure,
                           including the compiled embedded-license and theme resources.
  app-resources/           Exact Windows application manifest consumed by the relink target.
  build-inputs/            Original project scripts plus generated Qt/application build configuration.
  rebuild-qt.ps1           Rebuilds a replacement static /MT Qt from an extracted, optionally modified source tree.
  relink.ps1               Relinks the supplied application objects and libraries against a replacement static Qt prefix.

Example:
  .\rebuild-qt.ps1 -QtSource C:\work\qtbase-6.11.2 -QtBuild C:\work\qt-build -QtPrefix C:\work\qt-static
  .\relink.ps1 -QtPrefix C:\work\qt-static

The relink project uses qt_add_executable and qt_import_plugins so the replacement Qt supplies its own
Windows entrypoint and static-plugin import objects. The relink output is relink-build/FastPalette.exe.
The scripts require Windows x64, CMake, Ninja, Git,
and Visual Studio Build Tools with Desktop development with C++. The application materials are supplied
for recombination/relinking with a modified Qt library; their inclusion grants no additional license to
Fast Palette beyond rights required by the applicable library license or other applicable law.
"@ | Set-Content -LiteralPath (Join-Path $taskKitStage 'README.txt') -Encoding utf8

    $taskReport = @(
        'Fast Palette packaging report'
        "Generated (UTC): $([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))"
        "Executable source: $taskExecutable"
        "Executable bytes: $($taskExeInfo.Length)"
        "Executable SHA-256: $taskExeHash"
        "Qt version: $taskQtVersion"
        "Qt source revision: $taskRevision"
        'MSVC runtime: static /MT required by project and Qt configuration'
        "Imported DLLs: $($taskImports.Dlls -join ', ')"
        'Import policy: Windows system DLLs only; Qt, VCRUNTIME, MSVCP, and CONCRT DLL imports rejected'
        "Relink kit directory: $taskRelinkKit"
        "Relink kit archive: $taskRelinkArchive"
        ''
        'dumpbin /DEPENDENTS output:'
        ($taskImports.Output -join [Environment]::NewLine)
    )
    $taskReportPath = Join-Path $taskStage 'build-report.txt'
    $taskReport | Set-Content -LiteralPath $taskReportPath -Encoding utf8
    Copy-RequiredFile $taskReportPath (Join-Path $taskKitStage 'build-report.txt')

    Compress-Archive -LiteralPath $taskKitStage -DestinationPath $taskRelinkArchive -CompressionLevel Optimal

    Assert-DistributionReplaceable
    if (!(Test-Path -LiteralPath $taskDist)) {
        [void](New-Item -ItemType Directory -Path $taskDist)
    }

    foreach ($taskUnpublishedDirectory in @($taskRelinkKit, $taskPackageOutput)) {
        if (Test-Path -LiteralPath $taskUnpublishedDirectory) {
            throw "A packaging output appeared while staging; no existing directory was replaced: $taskUnpublishedDirectory"
        }
    }
    Move-Item -LiteralPath $taskKitStage -Destination $taskRelinkKit
    [void](New-Item -ItemType Directory -Path $taskPackageOutput)
    Copy-RequiredFile $taskReportPath (Join-Path $taskPackageOutput 'build-report.txt')
    "$taskExeHash *FastPalette.exe" | Set-Content -LiteralPath (Join-Path $taskPackageOutput 'FastPalette.exe.sha256') -Encoding ascii

    $taskStagedExecutable = Join-Path $taskDistStage 'FastPalette.exe'
    $taskFinalExecutable = Join-Path $taskDist 'FastPalette.exe'
    Move-Item -LiteralPath $taskStagedExecutable -Destination $taskFinalExecutable -Force

    $taskDistFiles = @(Get-ChildItem -LiteralPath $taskDist -File -Recurse)
    if ($taskDistFiles.Count -ne 1 -or $taskDistFiles[0].Name -ne 'FastPalette.exe') {
        throw 'Distribution postcondition failed: dist must contain exactly FastPalette.exe.'
    }
    $taskDistHash = (Get-FileHash -LiteralPath $taskDistFiles[0].FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($taskDistHash -ne $taskExeHash) { throw 'Distribution executable hash does not match the verified build output.' }

    Write-Output "Portable executable: $($taskDistFiles[0].FullName)"
    Write-Output "SHA-256: $taskExeHash"
    Write-Output "Build report: $(Join-Path $taskPackageOutput 'build-report.txt')"
    Write-Output "Relink kit: $taskRelinkKit"
    Write-Output "Relink archive: $taskRelinkArchive"
}
finally {
    if (Test-Path -LiteralPath $taskStage) {
        Assert-RepositoryChild $taskStage
        Remove-Item -LiteralPath $taskStage -Recurse -Force
    }
}
