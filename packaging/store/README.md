# Microsoft Store への提出（タスク 8-5）

Partner Center に入れるものを、ここにまとめる。**実際に入力するときは、`python tools/make_submission_guide.py` が作る `build/store/submission-guide.html`**（画面の順、コピーボタン付き）を開く。パッケージ（`.msix`）の作り方は `packaging/msix/README.md`。

| ファイル | 内容 |
|---|---|
| `listing.json` | 掲載情報（日本語・英語）。Partner Center の「ストア登録情報」に、言語ごとに貼る。`ui/src/storeListing.test.ts` が、字数の上限と言い方を確かめる |
| `logos/app-tile-300.png` | 1:1 のアプリのタイル（300 × 300。Microsoft が強く推奨）。`tools/make_icon.py` が書き出す |
| `privacy-policy.md` | プライバシーポリシー（日本語・英語）の元の文。プロダクトサイトに載せる（下の「プロパティ」）。文面を変えたら、サイトのほうも同じに直す |

字数の上限は Microsoft Learn の「Create app Store listings」（2026-11-21 に読んだ）による。検索語・著作権・追加のライセンス条項・開発者の上限はそのページになかったので、テストは短めの値で確かめている。Partner Center の画面に出る上限と違えば、テストを直す。

## 1. 登録と名前の予約（ユーザー）

1. Partner Center（https://partner.microsoft.com/dashboard）で、アプリの開発者として登録する。個人か会社かを選ぶ。発行者の表示名は **DE:ZIGN**（`CMakeLists.txt` の `AP_COMPANY_NAME` と同じにする）。
2. 「アプリとゲーム」→「新しい製品」→「MSIX または PWA アプリ」で、名前 **DE:ZIGN AUDIO PLAYER** を予約する。
3. 「製品 ID」（Product identity）の画面に出る 3 つの値を、Claude Code に渡す：
   - `Package/Identity/Name` → `AP_MSIX_IDENTITY_NAME`
   - `Package/Identity/Publisher`（`CN=` で始まる）→ `AP_MSIX_PUBLISHER`
   - `Package/Properties/PublisherDisplayName` → `AP_COMPANY_NAME`（DE:ZIGN のはず）

   Claude Code が `CMakeLists.txt` に入れ、MSIX を作り直す。

   済み（2026-11-22）：登録し、名前を予約した。値は `DEZIGN.DEZIGNAUDIOPLAYER`、`CN=955E5C32-0C31-4594-88D0-3D3367E883F3`、`DE:ZIGN`。

## 2. 価格と提供（Pricing and availability）

- 価格：無料。試用版なし。
- 市場：すべての市場。
- 公開：審査のあと、手動で公開する（最初の版は、公開の前にストアのページを確かめるため）。

## 3. プロパティ（Properties）

| 項目 | 値 |
|---|---|
| カテゴリ | 音楽（Music）。サブカテゴリはない |
| 個人情報を扱うか | **はい**（集めないし送らないが、規約 10.5.1 がデスクトップアプリを「個人情報に触れ得る製品」として扱う。`runFullTrust` の宣言からも「はい」に変えられ得る。2026-11-22 に「いいえ」から改めた） |
| プライバシーポリシーの URL | https://audio.dezign.jp/privacy/ （2026-11-22 に掲載を確かめた。文面を変えたら、提出の前にサイトも同じに直す） |
| Web サイト | https://audio.dezign.jp/ |
| サポートの連絡先 | https://github.com/DE-ZIGN/audio-player/issues |
| システム要件 | 書かない（特別な機器は要らない。DAC がなくても、Windows の出力デバイスで鳴る） |

製品の宣言（Product declarations）：

| 宣言 | 答え | 理由 |
|---|---|---|
| Store の決済を使わずに購入ができる | チェックしない | アプリの中に購入はない（寄付の窓口は配布ページ、8-6） |
| アクセシビリティの指針に沿って試験した | チェックしない | そのための試験をしていない（Microsoft の注意どおり） |
| 別のドライブ・取り外せる記憶装置に入れられる | チェックする（既定のまま） | 入れる場所に左右されない |
| OneDrive への自動バックアップに含められる | チェックする（既定のまま） | ライブラリと設定が戻せる |
| 生成 AI の機能を含む | チェックしない | ない |
| ペンとインク | チェックしない | ない |

## 4. 年齢区分（Age ratings、IARC の質問）

質問は最初の答え（アプリの種類）で変わる。答えの方針：

- 種類：ゲームではないアプリ（音楽の再生）。
- 暴力・性的な内容・言葉・薬物・賭博・恐怖の表現：なし（アプリが作る内容はない。表示するのは利用者自身の音楽ファイルのタグとジャケットだけ）。
- 利用者どうしのやり取り、利用者が作った内容の共有：なし。
- 位置情報の共有：なし。
- 個人情報の第三者への提供：なし。
- アプリ内の購入：なし。
- 制限のないインターネットへの接続（ブラウザーの機能）：なし（リンクは既定のブラウザーで開く）。

見込み：IARC 3+（すべての年齢）。

## 5. 提出の設定（Submission options）

### 制限付きの機能（Restricted capabilities）

マニフェストが `runFullTrust` を宣言しているので、理由を書かないと審査に通らない。英語で書く（欄の上限が公式のページにないので 500 字以内）：

> A Win32 desktop app (C++) packaged as MSIX, not UWP. Full trust is needed to open audio devices in WASAPI exclusive mode, to load ASIO drivers the user already installed (COM servers registered under HKLM\SOFTWARE\ASIO), and to read and watch music folders the user chooses, including network drives. It installs no drivers or services and changes no system settings. Source: https://github.com/DE-ZIGN/audio-player

### 審査の人への注記（Notes for certification）

> Notes of 2026-11-22. No account or sign-in is needed. To try playback: open Settings > Output and pick an output device (any Windows output works; a DAC is not required). Then add a music folder with the + next to "Watched folders" (on the left), or use "Open files". Formats: FLAC, WAV, AIFF, ALAC, MP3, AAC, DSF, DFF.
>
> By design, Pure mode (the default) converts nothing: if the device does not accept a track's format or rate, the app shows why and does not play it. Processing mode (Settings > Mode) converts such a track. In exclusive mode other apps' sound stops while this app plays.
>
> DSD is sent only as DoP. It is off by default; please leave it off unless the DAC supports DoP (DSD files then show why they are not played).
>
> Dependencies: the Microsoft Edge WebView2 Runtime, disclosed at the beginning of the description; without it the app says so at start. The app does not depend on any non-Microsoft driver or NT service and installs none. ASIO is optional, used only when the user selects a driver already installed.
>
> DE:ZIGN is the name of an individual developer, not a company. The app is free, with no purchases and no donation feature. It has no feature that connects to the internet; links open in the default browser.

## 6. 画面写真（8-5d）

- デスクトップ用：1366 × 768 以上（4K まで）、PNG、50 MB 以下、10 枚まで。字幕は `listing.json` の `screenshotCaptions`（言語ごと）。
- 実在のアルバムのジャケットや曲名を写さない（著作物のため）。テスト信号で作った曲と、作った名前・絵のライブラリで撮る。
- 日本語と英語の画面で、それぞれ撮る。
- できたものは `screenshots/ja-JP/` と `screenshots/en-US/`（1920 × 1080、5 枚ずつ）。ファイル名の番号が、字幕の順。
- 上げる前に、利用者名の入ったパスや実在の曲名が写っていないか見る。

撮り直すとき（PowerShell、リポジトリの直下で。`<作業用>` は空のフォルダー。M: は空いているドライブ文字）：

```
pip install numpy soundfile mutagen pillow websockets
python tools\make_store_demo.py <作業用>\Music
New-Item -ItemType Directory <作業用>\data
subst M: <作業用>
build\vs2022\tools\library\Release\ap_lib.exe scan M:\Music --db M:\data\library.db
build\vs2022\tools\library\Release\ap_lib.exe analyze --db M:\data\library.db
build\vs2022\tools\play\Release\ap_play.exe --list
python tools\store_screenshots.py --device-id "<ID>" --device-name "<名前>"
subst M: /d
```

- アプリは閉じてから始める。撮影中は、指定したデバイスから音が出る（音量とヘッドホンに注意）。24bit / 96 kHz を排他モードで受け付けるデバイスを選ぶ。
- `subst` は、監視フォルダーの欄に利用者名の入ったパスを写さないため。
- 出力の確認の画面には、そのデバイスの Windows の音量が写る。100% でなければ注意の印が付く。

## 7. パッケージ

- **版の先頭の数字は 1 以上、4 つ目は 0**（Microsoft Learn の App package requirements。WACK は検査しない）。パッケージを出し直すたびに `CMakeLists.txt` の版を上げる。
- 「パッケージ」の画面に、署名なしの `DEZIGNAudioPlayer-<版>-x64.msix` を上げる（Store が署名する）。ID を入れて作り直したものを使う（1 の 3）。
- 上げる前に、Windows App Certification Kit で検査する（`packaging/msix/README.md`）。
