# アプリの MSIX パッケージを作る（タスク 8-3c）。
#
#   powershell -ExecutionPolicy Bypass -File packaging\msix\build-msix.ps1
#       Store に出す、署名なしのパッケージ（Store が署名する）
#   powershell -ExecutionPolicy Bypass -File packaging\msix\build-msix.ps1 -Thumbprint <拇印>
#       手元で試す、署名付きのパッケージ（Publisher を証明書のものに置き換える）
#
# 先に cmake --build --preset release でアプリを作っておく。マニフェストは
# CMake が build\vs2022\packaging\msix\AppxManifest.xml に書き出している。

param(
    [string]$BuildDir = "",
    [string]$Configuration = "Release",
    [string]$OutDir = "",
    [string]$Thumbprint = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $root "build\vs2022" }
if (-not $OutDir) { $OutDir = Join-Path $root "build\msix" }
$stage = Join-Path $OutDir "stage"

# --- 道具 ------------------------------------------------------------------------
$sdkBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "x64\makeappx.exe") } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1
if (-not $sdkBin) { throw "Windows SDK（makeappx.exe）が見つかりません" }
$makeappx = Join-Path $sdkBin.FullName "x64\makeappx.exe"
$makepri = Join-Path $sdkBin.FullName "x64\makepri.exe"
$signtool = Join-Path $sdkBin.FullName "x64\signtool.exe"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT") -Directory |
    Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "VC++ ランタイム（再頒布用）が見つかりません" }

# --- マニフェスト ----------------------------------------------------------------
$manifestIn = Join-Path $BuildDir "packaging\msix\AppxManifest.xml"
if (-not (Test-Path $manifestIn)) { throw "マニフェストがありません。先に cmake で構成してください: $manifestIn" }
[xml]$manifest = Get-Content $manifestIn -Raw -Encoding UTF8
$exeName = $manifest.Package.Applications.Application.Executable
$version = $manifest.Package.Identity.Version
$kind = "store"
if ($Thumbprint) {
    # 手元で試すとき：署名する証明書の Subject と Publisher が一致していないと、
    # Windows はパッケージを入れない。
    $cert = Get-Item "Cert:\CurrentUser\My\$Thumbprint" -ErrorAction SilentlyContinue
    if (-not $cert) { throw "証明書が見つかりません（現在のユーザーの個人ストア）: $Thumbprint" }
    $manifest.Package.Identity.Publisher = $cert.Subject
    $kind = "test"
}

# --- 中身を並べる ----------------------------------------------------------------
$app = Join-Path $BuildDir "app\ap_app_artefacts\$Configuration"
if (-not (Test-Path (Join-Path $app $exeName))) { throw "$exeName がありません。先に cmake --build --preset release を実行してください: $app" }
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
# アプリ（exe、mpg123.dll、画面の ui フォルダー）
Copy-Item (Join-Path $app "*") $stage -Recurse
# VC++ ランタイム。パッケージの中に一緒に入れる（Microsoft の app-local の配り方）
foreach ($dll in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll") {
    Copy-Item (Join-Path $crt.FullName $dll) $stage
}
# 絵（tools/make_icon.py が作ったもの）と、言語ごとの文字
Copy-Item (Join-Path $PSScriptRoot "Assets") $stage -Recurse
Copy-Item (Join-Path $PSScriptRoot "Strings") $stage -Recurse
$manifestOut = Join-Path $stage "AppxManifest.xml"
$utf8 = New-Object System.Text.UTF8Encoding $false
$writer = New-Object System.IO.StreamWriter($manifestOut, $false, $utf8)
$manifest.Save($writer)
$writer.Close()

# --- resources.pri（言語ごとの文字と、大きさごとの絵の索引）---------------------
$priConfig = Join-Path $OutDir "priconfig.xml"
& $makepri createconfig /cf $priConfig /dq ja-JP /pv 10.0.0 /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makepri createconfig が失敗しました（$LASTEXITCODE）" }
# 既定の設定は、言語と倍率ごとに索引を別のファイルに分ける。分けた索引はバンドル
# （複数のパッケージを束ねたもの）でしか読まれないので、1 つの索引にまとめる。
[xml]$pri = Get-Content $priConfig -Raw -Encoding UTF8
foreach ($p in @($pri.SelectNodes("//packaging"))) { [void]$p.ParentNode.RemoveChild($p) }
$pri.Save($priConfig)
& $makepri new /pr $stage /cf $priConfig /mn $manifestOut /of (Join-Path $stage "resources.pri") /o | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makepri new が失敗しました（$LASTEXITCODE）" }
# 文字は resources.pri に入ったので、元のファイルはパッケージに入れない
Remove-Item -Recurse -Force (Join-Path $stage "Strings")

# --- 包む・署名する --------------------------------------------------------------
$base = [System.IO.Path]::GetFileNameWithoutExtension($exeName)
$suffix = ""
if ($kind -eq "test") { $suffix = "-test" }
$msix = Join-Path $OutDir "$base-$version-x64$suffix.msix"
if (Test-Path $msix) { Remove-Item -Force $msix }
& $makeappx pack /o /d $stage /p $msix | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makeappx pack が失敗しました（$LASTEXITCODE）" }

if ($kind -eq "test") {
    & $signtool sign /fd SHA256 /sha1 $Thumbprint /s My $msix
    if ($LASTEXITCODE -ne 0) { throw "signtool sign が失敗しました（$LASTEXITCODE）" }
}

Write-Host "作成しました（$kind）: $msix"
