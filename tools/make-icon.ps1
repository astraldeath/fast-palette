$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$taskBitmap=[Drawing.Bitmap]::new(256,256)
$taskGraphics=[Drawing.Graphics]::FromImage($taskBitmap)
$taskStream=[IO.MemoryStream]::new()
try {
 $taskGraphics.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
 $taskGraphics.Clear([Drawing.Color]::Transparent)
 $taskPath=[Drawing.Drawing2D.GraphicsPath]::new()
 $taskPath.AddArc(0,0,64,64,180,90)
 $taskPath.AddArc(192,0,64,64,270,90)
 $taskPath.AddArc(192,192,64,64,0,90)
 $taskPath.AddArc(0,192,64,64,90,90)
 $taskPath.CloseFigure()
 $taskBrush=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(32,35,41))
 $taskGraphics.FillPath($taskBrush,$taskPath)
 $taskPen=[Drawing.Pen]::new([Drawing.Color]::FromArgb(238,238,238),17)
 $taskPen.StartCap=[Drawing.Drawing2D.LineCap]::Round
 $taskPen.EndCap=[Drawing.Drawing2D.LineCap]::Round
 $taskGraphics.DrawLine($taskPen,56,76,107,128)
 $taskGraphics.DrawLine($taskPen,107,128,56,180)
 $taskGraphics.DrawLine($taskPen,143,180,201,180)
 $taskBitmap.Save($taskStream,[Drawing.Imaging.ImageFormat]::Png)
 $taskBytes=$taskStream.ToArray()
 $taskFile=[IO.File]::Create((Join-Path $PSScriptRoot '../resources/app.ico'))
 $taskWriter=[IO.BinaryWriter]::new($taskFile)
 try {
  $taskWriter.Write([uint16]0);$taskWriter.Write([uint16]1);$taskWriter.Write([uint16]1)
  $taskWriter.Write([byte]0);$taskWriter.Write([byte]0);$taskWriter.Write([byte]0);$taskWriter.Write([byte]0)
  $taskWriter.Write([uint16]1);$taskWriter.Write([uint16]32)
  $taskWriter.Write([uint32]$taskBytes.Length);$taskWriter.Write([uint32]22)
  $taskWriter.Write($taskBytes)
 } finally {$taskWriter.Dispose();$taskFile.Dispose()}
 $taskPen.Dispose();$taskBrush.Dispose();$taskPath.Dispose()
} finally {$taskStream.Dispose();$taskGraphics.Dispose();$taskBitmap.Dispose()}

foreach($taskVariant in @('dark','light')) {
 $taskMark=[Drawing.Bitmap]::new(48,48)
 $taskPainter=[Drawing.Graphics]::FromImage($taskMark)
 $taskInk=if($taskVariant -eq 'dark'){[Drawing.Color]::FromArgb(24,26,30)}else{[Drawing.Color]::FromArgb(248,249,251)}
 $taskStroke=[Drawing.Pen]::new($taskInk,5)
 try {
  $taskPainter.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $taskPainter.Clear([Drawing.Color]::Transparent)
  $taskStroke.StartCap=[Drawing.Drawing2D.LineCap]::Round
  $taskStroke.EndCap=[Drawing.Drawing2D.LineCap]::Round
  $taskPainter.DrawLine($taskStroke,12,24,21,33)
  $taskPainter.DrawLine($taskStroke,21,33,36,15)
  $taskMark.Save((Join-Path $PSScriptRoot "../resources/check-$taskVariant.png"),[Drawing.Imaging.ImageFormat]::Png)
 } finally {$taskStroke.Dispose();$taskPainter.Dispose();$taskMark.Dispose()}
}
