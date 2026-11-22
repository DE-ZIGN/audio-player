# パッケージの中の exe と DLL が読み込む DLL が、すべてパッケージの中か Windows に
# あることを確かめる（1.0.0 で msvcp140_2.dll が漏れた。2026-11-22）。
#
#   powershell -ExecutionPolicy Bypass -File packaging\msix\check-imports.ps1 -Dir <フォルダー>
#
# 開発機の System32 には Visual C++ の再頒布可能パッケージが入っているので、「この PC に
# あるか」では判定しない。Windows の部品は下の一覧に名前で持ち、増えたときは Windows に
# 標準で入っていることを確かめてから足す。LoadLibrary で読むものは分からない。

param([Parameter(Mandatory = $true)][string]$Dir)

$ErrorActionPreference = "Stop"

$windows = @(
    "advapi32.dll", "avrt.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll", "d2d1.dll",
    "d3d11.dll", "dcomp.dll", "dwmapi.dll", "dxgi.dll", "gdi32.dll", "imm32.dll",
    "kernel32.dll", "normaliz.dll", "ole32.dll", "oleaut32.dll", "shell32.dll",
    "shlwapi.dll", "user32.dll", "wininet.dll", "winmm.dll", "ws2_32.dll",
    # Windows N・KN には Media Feature Pack が要る（Store の説明文の先頭に書いてある）
    "mfplat.dll"
)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$dumpbin = Get-ChildItem (Join-Path $vs "VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe") |
    Sort-Object FullName | Select-Object -Last 1
if (-not $dumpbin) { throw "dumpbin.exe が見つかりません" }

$files = @(Get-ChildItem -LiteralPath $Dir -Recurse -File | Where-Object { $_.Extension -in ".exe", ".dll" })
if ($files.Count -eq 0) { throw "exe も DLL もありません: $Dir" }
# Windows が exe のために探すのは、パッケージの一番上のフォルダーだけ
$packed = @(Get-ChildItem -LiteralPath $Dir -File -Filter "*.dll" | ForEach-Object { $_.Name.ToLowerInvariant() })

$missing = @()
foreach ($file in $files) {
    $out = & $dumpbin.FullName /nologo /dependents $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "dumpbin が失敗しました: $($file.Name)" }
    $needs = @($out | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
    # 何も読み取れないのは、検査が働いていないとき
    if ($needs.Count -eq 0) { throw "読み込む DLL を読み取れません: $($file.Name)" }
    foreach ($need in $needs) {
        $name = $need.ToLowerInvariant()
        if ($packed -contains $name -or $windows -contains $name) { continue }
        # Windows 10 以降に入っている API セット（Universal CRT も）
        if ($name -like "api-ms-win-*") { continue }
        $missing += "$($file.Name) -> $need"
    }
}

if ($missing.Count -gt 0) {
    Write-Host "パッケージにも Windows にもない DLL を読み込んでいます："
    $missing | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "DLL の依存：$($files.Count) 個のファイルを調べ、漏れはありません"
exit 0
