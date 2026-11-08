# S2：MSIX の試作パッケージ

S1 の試作プレイヤー `ap_play` を MSIX に包み、パッケージの中でも排他 WASAPI と ASIO ドライバーの読み込みが動くかを確かめるためのもの。**配布用ではない。**

## 作り方

```
cmake --build --preset release
powershell -ExecutionPolicy Bypass -File packaging\msix-test\new-test-cert.ps1
powershell -ExecutionPolicy Bypass -File packaging\msix-test\build-msix.ps1 -Thumbprint <上で表示された拇印>
```

- `new-test-cert.ps1`：テスト用の自己署名証明書（`CN=AudioPlayer Test`、6か月）を、現在のユーザーの個人ストアに作る。公開鍵を `build\msix-test\ap-test.cer` に書き出す。
- `build-msix.ps1`：`build\msix-test\AudioPlayerS2Test.msix` を作って署名する。VC++ ランタイムはパッケージの中に一緒に入れる（app-local）。
- パッケージには `ap_play_msix` という別名が付く。インストール後に、ターミナルから `ap_play_msix --env` のように実行すると、パッケージの中で動く。

## インストール（初回だけ、管理者の操作が要る）

1. **証明書を信頼する（管理者の PowerShell で）**
   ```
   Import-Certificate -FilePath "<リポジトリ>\build\msix-test\ap-test.cer" -CertStoreLocation Cert:\LocalMachine\TrustedPeople
   ```
2. **インストール（通常の PowerShell で）**
   ```
   Add-AppxPackage "<リポジトリ>\build\msix-test\AudioPlayerS2Test.msix"
   ```

## 後片付け

```
Get-AppxPackage AudioPlayer.S2Test | Remove-AppxPackage
```

証明書も消すとき：

- 信頼の取り消し（管理者）：`Get-ChildItem Cert:\LocalMachine\TrustedPeople | Where-Object Subject -eq "CN=AudioPlayer Test" | Remove-Item`
- 署名用の証明書：`Get-ChildItem Cert:\CurrentUser\My | Where-Object Subject -eq "CN=AudioPlayer Test" | Remove-Item`
