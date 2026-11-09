# Builds and signs the S2 test MSIX from an existing CMake build.
#
#   powershell -ExecutionPolicy Bypass -File packaging\msix-test\build-msix.ps1 -Thumbprint <cert thumbprint>
#
# Prerequisites: cmake --build --preset release (ap_play), and a signing
# certificate from new-test-cert.ps1.

param(
    [Parameter(Mandatory = $true)][string]$Thumbprint,
    [string]$Configuration = "Release",
    [string]$Version = "0.1.0.0",
    [string]$Publisher = "CN=AudioPlayer Test"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$build = Join-Path $root "build\vs2022"
$out = Join-Path $root "build\msix-test"
$stage = Join-Path $out "stage"
$msix = Join-Path $out "AudioPlayerS2Test.msix"

# --- Tools -----------------------------------------------------------------
$sdkBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "x64\makeappx.exe") } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1
if (-not $sdkBin) { throw "Windows SDK (makeappx.exe) が見つかりません" }
$makeappx = Join-Path $sdkBin.FullName "x64\makeappx.exe"
$signtool = Join-Path $sdkBin.FullName "x64\signtool.exe"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT") -Directory |
    Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "VC++ ランタイム（再頒布用）が見つかりません" }

# --- Stage -------------------------------------------------------------------
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force (Join-Path $stage "Assets") | Out-Null

$exe = Join-Path $build "tools\play\$Configuration\ap_play.exe"
if (-not (Test-Path $exe)) { throw "ap_play.exe がありません。先に cmake --build --preset release を実行してください: $exe" }
Copy-Item $exe $stage
# libmpg123 (LGPL, a DLL; copied next to ap_play.exe by the build).
$mpg123 = Join-Path $build "tools\play\$Configuration\mpg123.dll"
if (-not (Test-Path $mpg123)) { throw "mpg123.dll がありません: $mpg123" }
Copy-Item $mpg123 $stage

# App-local VC++ runtime (Microsoft's documented app-local deployment).
foreach ($dll in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll") {
    Copy-Item (Join-Path $crt.FullName $dll) $stage
}

# Placeholder logos.
Add-Type -AssemblyName System.Drawing
function New-Logo([string]$path, [int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 40, 44, 52))
    $font = New-Object System.Drawing.Font "Segoe UI", ([single]($size * 0.32)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = "Center"; $fmt.LineAlignment = "Center"
    $g.DrawString("AP", $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF 0, 0, $size, $size), $fmt)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
New-Logo (Join-Path $stage "Assets\StoreLogo.png") 50
New-Logo (Join-Path $stage "Assets\Square44x44Logo.png") 44
New-Logo (Join-Path $stage "Assets\Square150x150Logo.png") 150

$manifest = Get-Content (Join-Path $PSScriptRoot "AppxManifest.xml.in") -Raw -Encoding UTF8
$manifest = $manifest.Replace("@PUBLISHER@", $Publisher).Replace("@VERSION@", $Version)
[System.IO.File]::WriteAllText((Join-Path $stage "AppxManifest.xml"), $manifest, (New-Object System.Text.UTF8Encoding $false))

# --- Pack and sign -------------------------------------------------------------
if (Test-Path $msix) { Remove-Item -Force $msix }
& $makeappx pack /o /d $stage /p $msix
if ($LASTEXITCODE -ne 0) { throw "makeappx pack が失敗しました（$LASTEXITCODE）" }

& $signtool sign /fd SHA256 /sha1 $Thumbprint /s My $msix
if ($LASTEXITCODE -ne 0) { throw "signtool sign が失敗しました（$LASTEXITCODE）" }

Write-Host "作成しました: $msix"
