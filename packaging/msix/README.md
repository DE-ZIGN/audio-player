# アプリの MSIX パッケージ（タスク 8-3c）

Microsoft Store に出すパッケージと、手元で試すパッケージを作る。S2 の試作（`ap_play` を包んだもの）は `packaging/msix-test/` に別にある。

## 中身

| ファイル | 内容 |
|---|---|
| `AppxManifest.xml.in` | マニフェストの雛形。CMake が `CMakeLists.txt` の値（表示名、発行者名、実行ファイル名、パッケージの ID、版）を入れて `build\vs2022\packaging\msix\AppxManifest.xml` に書き出す |
| `Strings\ja-JP\`、`Strings\en-US\` | 言語ごとの文字（今は説明文だけ）。既定は日本語 |
| `Assets\` | 絵。`tools/make_icon.py` が書き出す（手で直さない） |
| `build-msix.ps1` | パッケージを作るスクリプト |
| `check-imports.ps1` | 中身の exe と DLL が読み込む DLL に漏れがないかの検査。`build-msix.ps1` が包む前に呼ぶ（CI でも） |

- パッケージの ID（`AP_MSIX_IDENTITY_NAME`・`AP_MSIX_PUBLISHER`）は、Store で名前を予約したときに Partner Center が出した値（2026-11-22、8-5e）。ID が違うパッケージは Store が受け付けないので、変えない。
- VC++ ランタイムは、パッケージの中に一緒に入れる（app-local。S2 と同じ）。exe が使う DLL だけなので、コードを変えると増えることがある（`std::hypot` の 3 引数で `msvcp140_2.dll`）。開発機には再頒布可能パッケージが入っていて、手元への導入と WACK では漏れに気づけない。`check-imports.ps1` が落ちたら、それに従って足す。
- パッケージで動くアプリは、`%LOCALAPPDATA%` に**新しく作る**フォルダーやファイルが、パッケージ専用の場所（`%LOCALAPPDATA%\Packages\<パッケージ ファミリー名>\LocalCache\Local\`）に振り替えられる。パッケージを消すと、その分も消える。
- ただし、`%LOCALAPPDATA%\DEZIGN Audio Player` が**前からある**とき（ビルドした exe を直接動かしたことがある機械）は、パッケージ版もそのフォルダーをそのまま読み書きする。ライブラリと設定は、直接動かす版と同じものになり、パッケージを消しても残る（2026-11-21 に、開発者の機械で確かめた）。

## 作り方

```
cmake --preset vs2022
cmake --build --preset release
powershell -ExecutionPolicy Bypass -File packaging\msix\build-msix.ps1
```

`build\msix\DEZIGNAudioPlayer-<版>-x64.msix` ができる。署名はしない（Store が署名する）。CI でも、ビルドのたびにこれを作って、作れることを確かめている。

## 手元で試す

手元で入れるには、署名が要る。署名する証明書の Subject がマニフェストの Publisher と一致していないと Windows は入れないので、スクリプトが Publisher を証明書のものに置き換える。

```
powershell -ExecutionPolicy Bypass -File packaging\msix\build-msix.ps1 -Thumbprint <証明書の拇印>
```

`build\msix\DEZIGNAudioPlayer-<版>-x64-test.msix` ができる。

- テスト用の証明書は、S2 のものが使える（`packaging/msix-test/new-test-cert.ps1` が作り、拇印を表示する）。
- 証明書を信頼させる手順と、入れる手順は `packaging/msix-test/README.md` のとおり（ファイル名をこのパッケージのものに読み替える）。
- 消すとき：`Get-AppxPackage DEZIGN.DEZIGNAUDIOPLAYER | Remove-AppxPackage`
- 仮の ID のころ（2026-11-22 より前）に入れたものは、別のアプリとして残る。`Get-AppxPackage DEZIGN.AudioPlayer | Remove-AppxPackage` で消す。

## Store の検査（Windows App Certification Kit）

Windows SDK に入っている。管理者の権限で動かす。

```
& "${env:ProgramFiles(x86)}\Windows Kits\10\App Certification Kit\appcertui.exe"
```

パッケージの検査を選び、作った `.msix`（署名なしのもの）を指定する。
