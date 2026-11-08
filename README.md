# audio-player（仮称）

Windows 10/11 向けの、無料・オープンソースの高音質音楽プレイヤー（開発中）。

- **原音経路**：何も処理せず、ビットパーフェクトで DAC に届ける
- **音作り経路**：真空管サチュレーションや実在ホールの響きなどを、64bit 浮動小数点で加える
- **確かめられることだけを謳う**：経路表示、True Peak / LUFS メーター、ABX（ブラインドテスト）で、何をしているかと効果を自分で確かめられるようにする

現在は企画と設計の段階です。方針と調査資料は [`docs/`](docs/)、開発の前提は [`CLAUDE.md`](CLAUDE.md) にあります。

## ビルド

必要なもの：Visual Studio 2022（C++ デスクトップ開発）、CMake 3.25 以上、Git、Node.js LTS（画面のビルド用）。JUCE、Catch2、WebView2 SDK は初回の構成時に自動で取得します（SHA-256 で固定）。画面（`ui/`）はビルドのときに npm で作られ、アプリの隣の `ui/` フォルダーに置かれます。

```
cmake --preset vs2022
cmake --build --preset debug
ctest --preset debug
```

- アプリ：`build/vs2022/app/ap_app_artefacts/Debug/AudioPlayer.exe`
- 画面の開発：`npm --prefix ui run dev` で Vite の開発サーバーを起動し、`AudioPlayer.exe --dev-server` で開く（保存するとすぐ反映される）。ブラウザーで http://localhost:5173 を直接開くと、模擬エンジンで画面だけを確認できる。
- エンジンとテストだけをビルドする（JUCE を取得しない）：`vs2022-tests-only` の構成と、`tests-only-debug` のビルド・テスト

## ライセンス

[GNU Affero General Public License v3.0](LICENSE)

第三者のソフトウェアについては [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を参照してください。
