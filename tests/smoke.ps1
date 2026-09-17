param([string]$Executable = (Join-Path $PSScriptRoot '../build/Release/FastPalette.exe'))
$ErrorActionPreference='Stop'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class PaletteSmoke {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="FindWindowW")] public static extern IntPtr FindTitle(IntPtr c,string t);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr w,uint m,IntPtr p,IntPtr l);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint m,IntPtr p,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
}
"@
if([PaletteSmoke]::FindWindow('FastPalette.Window','Fast Palette message host') -ne [IntPtr]::Zero){throw 'Close Fast Palette before smoke testing.'}
$taskProcess=Start-Process -FilePath $Executable -ArgumentList '--background' -WindowStyle Hidden -PassThru
$taskHost=[IntPtr]::Zero
try {
 for($i=0;$i -lt 100;$i++){
  $taskHost=[PaletteSmoke]::FindWindow('FastPalette.Window','Fast Palette message host')
  $taskWindow=[PaletteSmoke]::FindTitle([IntPtr]::Zero,'Fast Palette')
  if($taskHost -ne [IntPtr]::Zero -and $taskWindow -ne [IntPtr]::Zero){break}
  if($taskProcess.HasExited){throw "Palette exited with $($taskProcess.ExitCode)"}
  Start-Sleep -Milliseconds 100
 }
 if($taskHost -eq [IntPtr]::Zero -or $taskWindow -eq [IntPtr]::Zero){throw 'Palette did not initialize'}
 [void][PaletteSmoke]::SendMessage($taskHost,0,[IntPtr]::Zero,[IntPtr]::Zero)
 if([PaletteSmoke]::IsWindowVisible($taskWindow)){throw 'Background launch unexpectedly visible'}
 [void][PaletteSmoke]::SendMessage($taskHost,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)
 if(![PaletteSmoke]::IsWindowVisible($taskWindow)){throw 'Hook invocation did not open palette'}
 [void][PaletteSmoke]::SendMessage($taskHost,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)
 if([PaletteSmoke]::IsWindowVisible($taskWindow)){throw 'Second invocation did not dismiss palette'}
 $taskSecond=Start-Process -FilePath $Executable -WindowStyle Hidden -PassThru
 if(!$taskSecond.WaitForExit(5000) -or $taskSecond.ExitCode -ne 0){throw 'Second instance failed'}
 Start-Sleep -Milliseconds 100
 if(![PaletteSmoke]::IsWindowVisible($taskWindow)){throw 'Second launch did not reopen palette'}
 $taskModules=@((Get-Process -Id $taskProcess.Id).Modules | ForEach-Object {$_.FileName})
 $taskExternal=@($taskModules | Where-Object { $_ -notlike "$env:WINDIR\*" -and $_ -ne (Resolve-Path -LiteralPath $Executable).Path })
 [PSCustomObject]@{Result='PASS';Checks='Background start, hook toggle, second-instance reopen';ExternalModules=$taskExternal}
} finally {
 if($taskHost -ne [IntPtr]::Zero){[void][PaletteSmoke]::PostMessage($taskHost,0x8005,[IntPtr]::Zero,[IntPtr]::Zero)}
 if(!$taskProcess.WaitForExit(15000)){throw 'Palette shutdown exceeded 15 seconds'}
}
