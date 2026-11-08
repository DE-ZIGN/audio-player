# Steinberg ASIO SDK（同梱分）

- 入手元：Steinberg「About ASIO (Open Source license variant)」<https://www.steinberg.net/developers/asiosdk-open/> の SDK Download（<https://www.steinberg.net/asiosdk> → `https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip`）
- 版：ASIO SDK 2.3.4（2025-10-15）
- ZIP の SHA-256：`d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca`
- 取得日：2026-11-08
- ライセンス：Steinberg ASIO License と GPLv3 のデュアルライセンス。本プロジェクトは **GPLv3** を選んで利用する（AGPLv3 第13条により組み合わせ可）。`LICENSE.txt` を参照。
- 同梱したファイル（いずれも**無改変**。`.gitattributes` で改行の変換も止めている）：
  - `common/asio.h`、`common/asiosys.h`、`common/iasiodrv.h`（ホストが使うインターフェースの定義）
  - `LICENSE.txt`、`README.md`、`changes.txt`
- 同梱していないもの：ロゴの画像、仕様書（`Steinberg ASIO SDK 2.3.pdf`）、ドライバーとホストのサンプル、`asio.cpp` などの実装ファイル（本プロジェクトは `IASIO` を直接使う）。

## 商標

ASIO は Steinberg Media Technologies GmbH の商標です。本プロジェクトは ASIO のロゴを使わず、製品名にも「ASIO」を含めない。画面と文書では、文字で「ASIO」と書くだけにとどめ、ライセンス画面に商標の表記を載せる。
