<#
  Deploy RenderDoc proxy DLL(s) into the Genshin Impact game directory.

  This is a 3DMigoto-style local proxy injection:
    - d3d11.dll proxy  -> loads rdhelper.dll and lets RenderDoc wrap D3D11.
    - dxgi.dll proxy   -> loads rdhelper.dll and lets RenderDoc hook DXGI.

  The script writes renderdoc_proxy_rdoc_path.txt next to the proxy so the
  proxy can locate rdhelper.dll without copying RenderDoc's runtime into the
  game directory.

  By default this only previews what would happen. Pass -Apply to make changes.
#>
param(
  [string]$GameDir = '',
  [ValidateSet('d3d11','dxgi','both')]
  [string]$Proxy = 'both',
  [ValidateSet('Development','Release')]
  [string]$Configuration = 'Release',
  [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot ('x64\' + $Configuration)

function Find-GenshinDir {
  if($GameDir -and (Test-Path (Join-Path $GameDir 'YuanShen.exe'))) { return (Resolve-Path $GameDir).Path }
  if($GameDir -and (Test-Path (Join-Path $GameDir 'GenshinImpact.exe'))) { return (Resolve-Path $GameDir).Path }

  $known = @(
    'D:\yuanshen\miHoYo Launcher\games\Genshin Impact Game',
    'C:\Program Files\Genshin Impact\Genshin Impact Game',
    'C:\Program Files\HoYoPlay\games\Genshin Impact Game'
  )

  foreach($dir in $known) {
    if((Test-Path (Join-Path $dir 'YuanShen.exe')) -or (Test-Path (Join-Path $dir 'GenshinImpact.exe'))) {
      return (Resolve-Path $dir).Path
    }
  }

  $candidates = foreach($root in @('C:\Program Files','C:\Program Files (x86)','D:\','E:\')) {
    if(Test-Path $root) {
      Get-ChildItem -Path $root -Depth 4 -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -in @('YuanShen.exe','GenshinImpact.exe') } |
        Select-Object -ExpandProperty DirectoryName
    }
  }

  return $candidates | Select-Object -First 1
}

$gameDir = Find-GenshinDir
if(-not $gameDir) {
  throw 'Could not find Genshin Impact. Pass -GameDir explicitly.'
}

$rdhelper = Join-Path $buildDir 'rdhelper.dll'
if(-not (Test-Path $rdhelper)) {
  throw "rdhelper.dll not found: $rdhelper"
}

Write-Host "Game directory : $gameDir"
Write-Host "Build directory: $buildDir"
Write-Host "Mode           : $Proxy"
Write-Host "Apply          : $Apply"

$targets = @()
if($Proxy -in @('d3d11','both')) {
  $targets += [pscustomobject]@{ Name='d3d11.dll'; Source=Join-Path $buildDir 'd3d11_proxy64.dll' }
}
if($Proxy -in @('dxgi','both')) {
  $targets += [pscustomobject]@{ Name='dxgi.dll'; Source=Join-Path $buildDir 'dxgi_proxy64.dll' }
}

foreach($t in $targets) {
  if(-not (Test-Path $t.Source)) {
    throw "Proxy source not found: $($t.Source)"
  }
}

$configFile = Join-Path $gameDir 'renderdoc_proxy_rdoc_path.txt'
$configContent = (Resolve-Path $rdhelper).Path

if(-not $Apply) {
  Write-Host ''
  Write-Host 'Planned changes:'
  foreach($t in $targets) {
    $dest = Join-Path $gameDir $t.Name
    $existing = if(Test-Path $dest) { ' (existing file would be backed up)' } else { '' }
    Write-Host "  copy $($t.Source) -> $dest$existing"
  }
  Write-Host "  write $configFile -> $configContent"
  Write-Host ''
  Write-Host 'Re-run with -Apply to apply these changes.'
  exit 0
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'

foreach($t in $targets) {
  $dest = Join-Path $gameDir $t.Name
  if(Test-Path $dest) {
    $backup = "$dest.renderdoc_bak_$stamp"
    Write-Host "Backing up $dest -> $backup"
    Move-Item -LiteralPath $dest -Destination $backup -Force
  }
  Write-Host "Copying $($t.Source) -> $dest"
  Copy-Item -LiteralPath $t.Source -Destination $dest -Force
}

Write-Host "Writing $configFile"
[System.IO.File]::WriteAllText($configFile, $configContent, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host 'Done. Start Genshin Impact normally. To restore, remove:'
foreach($t in $targets) {
  Write-Host "  $(Join-Path $gameDir $t.Name)"
}
Write-Host "and delete $configFile"
