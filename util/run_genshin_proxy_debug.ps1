<#
  Launch Genshin Impact with RenderDoc proxy debug logging enabled.

  This does not modify game files. It only sets environment variables for the
  child process so the d3d11/dxgi proxies write their logs next to the game.

  Default is dry-run. Pass -Launch to actually start the game.
#>
param(
  [string]$GameDir = '',
  [ValidateSet('Development','Release')]
  [string]$Configuration = 'Release',
  [switch]$Launch
)

$ErrorActionPreference = 'Stop'

function Find-GenshinExe {
  $known = @(
    'D:\yuanshen\miHoYo Launcher\games\Genshin Impact Game',
    'C:\Program Files\Genshin Impact\Genshin Impact Game',
    'C:\Program Files\HoYoPlay\games\Genshin Impact Game'
  )

  if($GameDir) {
    foreach($name in @('YuanShen.exe','GenshinImpact.exe')) {
      $p = Join-Path $GameDir $name
      if(Test-Path $p) { return (Resolve-Path $p).Path }
    }
  }

  foreach($dir in $known) {
    foreach($name in @('YuanShen.exe','GenshinImpact.exe')) {
      $p = Join-Path $dir $name
      if(Test-Path $p) { return (Resolve-Path $p).Path }
    }
  }

  throw 'Could not find YuanShen.exe / GenshinImpact.exe. Pass -GameDir explicitly.'
}

$exe = Find-GenshinExe
$gameDir = Split-Path -Parent $exe
$rdhelper = Join-Path (Split-Path -Parent $PSScriptRoot) ("x64\" + $Configuration + "\rdhelper.dll")
$rdhelper = (Resolve-Path $rdhelper).Path

$logFiles = @(
  (Join-Path $gameDir 'd3d11_proxy.log'),
  (Join-Path $gameDir 'dxgi_proxy.log'),
  (Join-Path $gameDir 'd3d11_proxy_renderdoc.log'),
  (Join-Path $gameDir 'dxgi_proxy_renderdoc.log')
)

Write-Host "Game exe     : $exe"
Write-Host "RenderDoc    : $rdhelper"
Write-Host "Proxy logs   :"
$logFiles | ForEach-Object { Write-Host "  $_" }

if(-not $Launch) {
  Write-Host ''
  Write-Host 'Dry run. Re-run with -Launch to start the game.'
  exit 0
}

$env:D3D11_PROXY_DEBUG = '1'
$env:DXGI_PROXY_DEBUG = '1'
$env:RENDERDOC_RDOC_PATH = $rdhelper

$p = Start-Process -FilePath $exe -WorkingDirectory $gameDir -PassThru
Write-Host ''
Write-Host "Started PID $($p.Id)"
Write-Host 'Wait a few seconds, then inspect the proxy logs above.'
