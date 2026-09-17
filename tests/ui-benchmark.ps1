param([string]$Executable=(Join-Path $PSScriptRoot '../build/Release/FastPalette.exe'))
$ErrorActionPreference='Stop'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class PaletteTiming {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="FindWindowW")] public static extern IntPtr FindTitle(IntPtr c,string t);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr w,uint m,IntPtr p,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
 [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr w,IntPtr rect,IntPtr region,uint flags);
 [DllImport("dwmapi.dll")] public static extern int DwmFlush();
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint m,IntPtr p,IntPtr l);
}
"@
if([PaletteTiming]::FindWindow('FastPalette.Window','Fast Palette message host') -ne [IntPtr]::Zero){throw 'Close Fast Palette before benchmarking.'}
$taskCold=[Diagnostics.Stopwatch]::StartNew()
$taskApp=Start-Process -FilePath $Executable -ArgumentList '--background' -WindowStyle Hidden -PassThru
$taskHost=[IntPtr]::Zero
try {
 for($i=0;$i -lt 100;$i++){
  $taskHost=[PaletteTiming]::FindWindow('FastPalette.Window','Fast Palette message host')
  $taskWindow=[PaletteTiming]::FindTitle([IntPtr]::Zero,'Fast Palette')
  if($taskHost -ne [IntPtr]::Zero -and $taskWindow -ne [IntPtr]::Zero){break}
  Start-Sleep -Milliseconds 20
 }
 if($taskWindow -eq [IntPtr]::Zero){throw 'No palette window'}
 [void][PaletteTiming]::SendMessage($taskHost,0,[IntPtr]::Zero,[IntPtr]::Zero)
 $taskCold.Stop()
 # Let asynchronous indexing finish before warm measurements.
 Start-Sleep -Milliseconds 2500
 $taskWarm=@();$taskPresentation=@()
 for($i=0;$i -lt 25;$i++){
  [void][PaletteTiming]::SendMessage($taskWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
  $taskWatch=[Diagnostics.Stopwatch]::StartNew()
  [void][PaletteTiming]::SendMessage($taskHost,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)
  $taskRequest=$taskWatch.Elapsed.TotalMilliseconds
  if(![PaletteTiming]::IsWindowVisible($taskWindow)){throw 'Palette did not open'}
  [void][PaletteTiming]::RedrawWindow($taskWindow,[IntPtr]::Zero,[IntPtr]::Zero,0x181)
  [void][PaletteTiming]::DwmFlush()
  $taskWatch.Stop()
  if($i -ge 5){$taskWarm+=$taskRequest;$taskPresentation+=$taskWatch.Elapsed.TotalMilliseconds}
 }
 [void][PaletteTiming]::SendMessage($taskWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 $taskBefore=(Get-Process -Id $taskApp.Id).TotalProcessorTime.TotalMilliseconds
 Start-Sleep -Milliseconds 2000
 $taskMetrics=Get-Process -Id $taskApp.Id
 $taskIdle=$taskMetrics.TotalProcessorTime.TotalMilliseconds-$taskBefore
 $taskWarm=@($taskWarm|Sort-Object);$taskPresentation=@($taskPresentation|Sort-Object)
 [PSCustomObject]@{
  StartupWindowReadyMs=[Math]::Round($taskCold.Elapsed.TotalMilliseconds,2)
  WarmRequestP50Ms=[Math]::Round($taskWarm[9],3)
  WarmRequestP95Ms=[Math]::Round($taskWarm[18],3)
  RepaintAndDwmFlushP50Ms=[Math]::Round($taskPresentation[9],3)
  RepaintAndDwmFlushP95Ms=[Math]::Round($taskPresentation[18],3)
  WorkingSetMB=[Math]::Round($taskMetrics.WorkingSet64/1MB,1)
  IdleCpuMsOver2Seconds=[Math]::Round($taskIdle,2)
 } | Format-List
}finally{
 if($taskHost -ne [IntPtr]::Zero){[void][PaletteTiming]::PostMessage($taskHost,0x8005,[IntPtr]::Zero,[IntPtr]::Zero)}
 if(!$taskApp.WaitForExit(15000)){throw 'Shutdown timed out'}
}
