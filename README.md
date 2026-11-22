# DE:ZIGN AUDIO PLAYER

Windows 10/11（x64）向けの、無料・オープンソースの音楽プレイヤー。読みは「デザイン オーディオプレーヤー」。実行ファイルは `DEZIGNAudioPlayer.exe`。

- **原音モード**：音量 100%・ISP セーフがオフ（既定）なら、FLAC などの整数の音源は値を変えずにドライバーへ渡す（ビットパーフェクト）。MP3・AAC など浮動小数点で扱う音源は、出力が浮動小数点を受け付けなければディザをかけて整数にする
- **音作りモード**：ReplayGain、EQ、真空管、トーン、空間（IR の畳み込み）、クロスフィード、リサンプラーを、64bit 浮動小数点で処理する
- **確かめられることだけを謳う**：経路表示、True Peak / LUFS メーター、ABX（ブラインドテスト）で、何をしているかを自分で確かめられる

## 入手

配布は、初回は **Microsoft Store だけ**です。**まだ公開していません**（公開したら、ここにリンクを載せます）。このリポジトリの Releases や、ほかのサイトでは実行ファイルを配っていません。

- 必要なもの：Windows 10（バージョン 1809 以降）か Windows 11（どちらも x64）、Microsoft Edge WebView2 ランタイム。Windows の N・KN エディションでは Media Feature Pack も必要です。
- **DSD を DoP で送るときの注意**：DAC が DoP に対応しているかは自動では分かりません。対応していない DAC では大きな雑音になるので、オンにしないでください（既定はオフ）。
- プライバシーポリシー：https://audio.dezign.jp/privacy/
- 不具合の報告：https://github.com/DE-ZIGN/audio-player/issues

方針と調査資料は [`docs/`](docs/)、開発の前提は [`CLAUDE.md`](CLAUDE.md) にあります。

## ビルド

必要なもの：Visual Studio 2022（C++ デスクトップ開発）、CMake 3.25 以上、Git、Node.js LTS（画面のビルド用）。JUCE、WebView2 SDK、libFLAC、mpg123、libebur128、r8brain-free-src、SQLite、TagLib（と、テスト用の Catch2、LAME）は初回の構成時に自動で取得します（SHA-256 で固定）。mpg123 は `mpg123.dll` として exe の隣に置かれます。ASIO SDK と ALAC のデコーダーは `third_party/` に同梱しています。画面（`ui/`）はビルドのときに npm で作られ、アプリの隣の `ui/` フォルダーに置かれます。

```
cmake --preset vs2022
cmake --build --preset debug
ctest --preset debug
```

- アプリ：`build/vs2022/app/ap_app_artefacts/Debug/DEZIGNAudioPlayer.exe`（ライブラリと設定は `%LOCALAPPDATA%\DEZIGN Audio Player`）
- 画面の開発：`npm --prefix ui run dev` で Vite の開発サーバーを起動し、`DEZIGNAudioPlayer.exe --dev-server` で開く（保存するとすぐ反映される）。ブラウザーで http://localhost:5173 を直接開くと、模擬エンジンで画面だけを確認できる。
- エンジンとテストだけをビルドする（JUCE を取得しない）：`vs2022-tests-only` の構成と、`tests-only-debug` のビルド・テスト
- AddressSanitizer（メモリーの誤った読み書きを見つける仕組み）を入れてテストする：`cmake --preset vs2022-asan`、`cmake --build --preset asan`、`ctest --preset asan`。デコーダーは外から来るファイルを読むので、デコーダーに手を入れたらこれも通す。
- MSIX パッケージ（Store に出すもの・手元で試すもの）を作る：[`packaging/msix/README.md`](packaging/msix/README.md)
- 自動ビルド：main へのプッシュとプルリクエストのたびに、GitHub Actions（[`.github/workflows/build.yml`](.github/workflows/build.yml)）が Release 版をビルドして、上と同じテスト（`ctest --preset release`）を走らせる。資料（`docs/` と Markdown）だけの変更では走らない。

## ライセンス

[GNU Affero General Public License v3.0](LICENSE)

第三者のソフトウェアについては [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を参照してください。

ASIO は Steinberg Media Technologies GmbH の商標です。本製品は ASIO のロゴを使っていません。
