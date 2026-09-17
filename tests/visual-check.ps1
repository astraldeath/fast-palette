$ErrorActionPreference='Stop'
# Both executables render the actual Qt widgets and save their backing stores.
& (Join-Path $PSScriptRoot '../build/Release/qt_ui_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Palette visual checks failed'}
& (Join-Path $PSScriptRoot '../build/Release/qt_settings_tests.exe')
if($LASTEXITCODE -ne 0){throw 'Settings visual checks failed'}
Get-Item (Join-Path $PSScriptRoot '../build/palette-preview.png'),(Join-Path $PSScriptRoot '../build/settings-preview.png')
