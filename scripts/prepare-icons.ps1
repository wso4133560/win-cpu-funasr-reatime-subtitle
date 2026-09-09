param(
  [Parameter(Mandatory=$true)][string]$TraySource,
  [Parameter(Mandatory=$true)][string]$AppSource,
  [string]$OutputDir = (Join-Path $PSScriptRoot '..\assets')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore,WindowsBase
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Read-Bgra([string]$Path) {
  $stream = [IO.File]::OpenRead($Path)
  try {
    $decoder = [Windows.Media.Imaging.PngBitmapDecoder]::new($stream,
      [Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
      [Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
    $frame = $decoder.Frames[0]
    $converted = [Windows.Media.Imaging.FormatConvertedBitmap]::new($frame,
      [Windows.Media.PixelFormats]::Bgra32, $null, 0)
    $width = $converted.PixelWidth; $height = $converted.PixelHeight; $stride = $width * 4
    $pixels = [byte[]]::new($stride * $height); $converted.CopyPixels($pixels, $stride, 0)
    return @{ Width=$width; Height=$height; Stride=$stride; Pixels=$pixels }
  } finally { $stream.Dispose() }
}

function Remove-Checkerboard($image) {
  $w=$image.Width; $h=$image.Height; $p=$image.Pixels
  $candidate = [bool[]]::new($w*$h)
  for($y=0;$y -lt $h;$y++){ for($x=0;$x -lt $w;$x++){
    $i=($y*$w+$x)*4; $b=$p[$i]; $g=$p[$i+1]; $r=$p[$i+2]
    $max=[Math]::Max($r,[Math]::Max($g,$b)); $min=[Math]::Min($r,[Math]::Min($g,$b)); $avg=($r+$g+$b)/3
    $candidate[$y*$w+$x]=(($max-$min) -le 18 -and $avg -ge 75 -and $avg -le 245)
  }}
  $seen=[bool[]]::new($w*$h); $queue=[Collections.Generic.Queue[int]]::new()
  function Add-Background([int]$n) { if($candidate[$n] -and -not $seen[$n]){$seen[$n]=$true;$queue.Enqueue($n)} }
  for($x=0;$x -lt $w;$x++){ Add-Background $x; Add-Background (($h-1)*$w+$x) }
  for($y=1;$y -lt ($h-1);$y++){ Add-Background ($y*$w); Add-Background ($y*$w+$w-1) }
  while($queue.Count){
    $n=$queue.Dequeue()
    $x=$n%$w; $y=[Math]::Floor($n/$w)
    if($x -gt 0){Add-Background ($n-1)}; if($x+1 -lt $w){Add-Background ($n+1)}
    if($y -gt 0){Add-Background ($n-$w)}; if($y+1 -lt $h){Add-Background ($n+$w)}
  }
  for($n=0;$n -lt $seen.Length;$n++){if($seen[$n]){$p[$n*4+3]=0}}
  return $image
}

function Save-Png($image,[string]$Path,[int]$Size) {
  $source=[Windows.Media.Imaging.BitmapSource]::Create($image.Width,$image.Height,96,96,
    [Windows.Media.PixelFormats]::Bgra32,$null,$image.Pixels,$image.Stride)
  $scale=[Windows.Media.Imaging.TransformedBitmap]::new(); $scale.BeginInit(); $scale.Source=$source
  $scale.Transform=[Windows.Media.ScaleTransform]::new($Size/$image.Width,$Size/$image.Height); $scale.EndInit()
  $encoder=[Windows.Media.Imaging.PngBitmapEncoder]::new(); $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($scale))
  $out=[IO.File]::Create($Path); try{$encoder.Save($out)}finally{$out.Dispose()}
}

function Write-Ico([string]$Path,[string]$PngDir,[string]$Stem) {
  $sizes=16,20,24,32,48,64,128,256; $payloads=@()
  foreach($size in $sizes){$payloads += ,([IO.File]::ReadAllBytes((Join-Path $PngDir "$Stem-$size.png")))}
  $offset=6+16*$sizes.Count; $out=[IO.File]::Create($Path)
  try { $bw=[IO.BinaryWriter]::new($out); $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
    for($i=0;$i -lt $sizes.Count;$i++){ $size=$sizes[$i]; $dim=if($size -eq 256){0}else{$size}
      $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([uint16]1); $bw.Write([uint16]32)
      $bw.Write([uint32]$payloads[$i].Length); $bw.Write([uint32]$offset); $offset += $payloads[$i].Length }
    foreach($payload in $payloads){$bw.Write($payload)}; $bw.Dispose()
  } finally {$out.Dispose()}
}

foreach($item in @(@{Source=$TraySource;Stem='funasr-tray'},@{Source=$AppSource;Stem='funasr-app'})){
  $image=Remove-Checkerboard (Read-Bgra $item.Source)
  foreach($size in 16,20,24,32,48,64,128,256){Save-Png $image (Join-Path $OutputDir "$($item.Stem)-$size.png") $size}
  Copy-Item (Join-Path $OutputDir "$($item.Stem)-256.png") (Join-Path $OutputDir "$($item.Stem).png") -Force
  Write-Ico (Join-Path $OutputDir "$($item.Stem).ico") $OutputDir $item.Stem
}
Write-Output "Created icon assets in $OutputDir"
