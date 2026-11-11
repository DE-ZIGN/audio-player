# THIRD_PARTY_NOTICES（第三者のソフトウェア）

このプロジェクトが使っている第三者のソフトウェアと、その著作権表示・ライセンスの一覧です。
本プロジェクト自体のライセンスは AGPLv3 です（`LICENSE` を参照）。

## 書き方のルール

- 依存を追加したら、同じコミットでここに追記する。`cmake/Dependencies.cmake` の固定バージョンと一致させる。
- 1部品1節。項目は下の順にそろえる（フェーズ1の 8-2 で、アプリ内のライセンス画面をここから自動生成する予定）。
- 「配布物」：アプリに組み込まれて利用者に配られるなら「含む」、ビルドやテストにしか使わないなら「含まない」。
- ライセンス本文は各部品のソースに含まれるファイルを正とする。アプリ内のライセンス画面には本文を全文載せる（8-2）。

---

## JUCE

- バージョン：8.0.17（タグ `8.0.17-security`、コミット `e548e97a54a083acbfc330d584419fd074409045`）
- ライセンス：AGPL-3.0-only（JUCE は AGPLv3 と商用ライセンスのデュアルライセンス。本プロジェクトは AGPLv3 で利用）
- 配布物：含む
- 入手先：https://github.com/juce-framework/JUCE
- 著作権表示：Copyright (c) Raw Material Software Limited
- ライセンス本文：JUCE の `LICENSE.md`、https://www.gnu.org/licenses/agpl-3.0.html
- 使っているモジュール：`juce_core`、`juce_events`、`juce_graphics`、`juce_gui_basics`、`juce_gui_extra`（WebView）

以下は、上記のモジュールに同梱されていてアプリに組み込まれる部品です（JUCE の `LICENSE.md` の一覧による）。

- 画面（ui/）には、JUCE の JavaScript ヘルパー（`juce_gui_extra/native/javascript` の `index.js` と `check_native_interop.js`）を `ui/src/juce/` に写して使っている。ライセンスは上と同じ（AGPLv3 で利用）。

### zlib（`juce_core` に同梱）

- バージョン：1.3.1
- ライセンス：Zlib
- 配布物：含む
- 入手先：https://zlib.net/
- 著作権表示：Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler
- ライセンス本文：JUCE `modules/juce_core/zip/zlib/README`

### libpng（`juce_graphics` に同梱）

- バージョン：1.6.37
- ライセンス：libpng-2.0（PNG Reference Library License version 2。1.6.35 以前の部分は version 1）
- 配布物：含む
- 入手先：http://www.libpng.org/pub/png/libpng.html
- 著作権表示：
  - Copyright (c) 1995-2019 The PNG Reference Library Authors.
  - Copyright (c) 2018-2019 Cosmin Truta.
  - Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
  - Copyright (c) 1996-1997 Andreas Dilger.
  - Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.
- ライセンス本文：JUCE `modules/juce_graphics/image_formats/pnglib/LICENSE`

### Independent JPEG Group's JPEG software（`juce_graphics` に同梱）

- バージョン：9f
- ライセンス：IJG
- 配布物：含む
- 入手先：https://www.ijg.org/
- 著作権表示：Copyright (C) 1991-2024, Thomas G. Lane, Guido Vollbeding.
  This software is based in part on the work of the Independent JPEG Group.
- ライセンス本文：JUCE `modules/juce_graphics/image_formats/jpglib/README`

### HarfBuzz（`juce_graphics` に同梱）

- バージョン：10.1.0
- ライセンス：MIT-Modern-Variant（HarfBuzz の言う "Old MIT"）
- 配布物：含む
- 入手先：https://github.com/harfbuzz/harfbuzz
- 著作権表示：Copyright © 2010-2022 Google, Inc. ほか（全文は `COPYING` を参照）
- ライセンス本文：JUCE `modules/juce_graphics/fonts/harfbuzz/COPYING`

### SheenBidi（`juce_graphics` に同梱）

- バージョン：JUCE 8.0.17 同梱版
- ライセンス：Apache-2.0
- 配布物：含む
- 入手先：https://github.com/Tehreer/SheenBidi
- 著作権表示：Copyright (C) 2014-2025 Muhammad Tayyab Akram
- ライセンス本文：JUCE `modules/juce_graphics/unicode/sheenbidi/LICENSE`

---

## Steinberg ASIO SDK

- バージョン：2.3.4（2025-10-15）
- ライセンス：GPL-3.0-only（Steinberg ASIO License とのデュアルライセンスのうち GPLv3 を選択）
- 配布物：含む（`third_party/asiosdk/` のヘッダー3つ。ASIO 出力の実装に使う）
- 入手先：https://www.steinberg.net/developers/asiosdk-open/
- 著作権表示：(c) 2025, Steinberg Media Technologies GmbH, All Rights Reserved
- ライセンス本文：`third_party/asiosdk/LICENSE.txt`、https://www.gnu.org/licenses/gpl-3.0.html
- 商標：ASIO は Steinberg Media Technologies GmbH の商標です。本製品は ASIO のロゴを使っていません。
- 同梱の詳細：`third_party/asiosdk/VENDORED.md`

---

## libFLAC

- バージョン：1.5.0（公式のソース配布 `flac-1.5.0.tar.xz`、SHA-256 `f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920`）
- ライセンス：BSD-3-Clause（`COPYING.Xiph`）
- 配布物：含む（FLAC のデコードに使う。C のライブラリ libFLAC だけを組み込み、`flac` などのコマンドラインのプログラム（GPL）、libFLAC++、Ogg 対応は使わない）
- 入手先：https://xiph.org/flac/ 、https://github.com/xiph/flac
- 著作権表示：
  - Copyright (C) 2000-2009 Josh Coalson
  - Copyright (C) 2011-2025 Xiph.Org Foundation
- ライセンス本文：libFLAC の `COPYING.Xiph`
- テストでは、テスト用の FLAC ファイルを作るために libFLAC のエンコーダーも使う（同じライブラリ）。

---

## Apple Lossless Audio Codec（ALAC）

- バージョン：macosforge/alac のコミット `c38887c5c5e64a4b31108733bd79ca9b2496d987`（2016-05-11。リポジトリは保管扱いで、コーデックのコードは 2011 年のもの）
- ライセンス：Apache-2.0
- 配布物：含む（デコーダーだけ。`third_party/alac/codec/` の ALACDecoder ほか）。エンコーダー（ALACEncoder ほか）はテスト用のファイルを作るためだけに使い、配布物には含まない
- 入手先：https://github.com/macosforge/alac
- 著作権表示：Copyright (c) 2011 Apple Inc. All rights reserved.
- ライセンス本文：`third_party/alac/LICENSE`
- **変更あり**：公開版のデコーダーには入力の検査がなく、同じコードを使った他社のデコーダーで脆弱性が見つかっている（CVE-2021-0674 / 0675 / 30351）。値の検査を足した。変更点の一覧と元のファイルのハッシュは `third_party/alac/VENDORED.md`（Apache 2.0 の「変更したことを示す」義務もここで満たす）。
- 上流の `codec/` には Apple Public Source License 2.0 の本文（`APPLE_LICENSE.txt`）も置かれているが、使っているファイルはどれも Apache 2.0 のヘッダーを持ち、それを参照していないため同梱していない。

---

## mpg123（libmpg123）

- バージョン：1.33.7（公式のソース配布 `mpg123-1.33.7.tar.bz2`、SHA-256 `31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee`）
- ライセンス：LGPL-2.1（`COPYING`）
- 配布物：含む（`mpg123.dll` として、アプリの exe の隣に置く。MP3 のデコードに使う。動的リンクなので、利用者が同じ API の別のビルドに差し替えられる）
- 入手先：https://www.mpg123.de/ （ソースは上の公式配布物。変更なし）
- 著作権表示：Copyright (c) 1995-2020 by Michael Hipp and others, free software under the terms of the LGPL v2.1（`COPYING` に指定された表示。著者の一覧は `AUTHORS`）
- ライセンス本文：mpg123 の `COPYING`
- ビルドの設定：mpg123 の CMake（`ports/cmake`）で、デコードのライブラリだけを作る（libout123、プログラム、ネットワーク機能は作らない）。アセンブラーを使わない C だけのデコーダーにしている（どの CPU でも同じ結果になり、テストで値を確かめられるため）。

---

## libebur128

- バージョン：1.2.6（リリースのタグ `v1.2.6` のソースの配布 `libebur128-1.2.6.tar.gz`、SHA-256 `baa7fc293a3d4651e244d8022ad03ab797ca3c2ad8442c43199afe8059faa613`）
- ライセンス：MIT（`COPYING`）。同梱の `queue.h`（`ebur128/queue/sys/queue.h`）は BSD-3-Clause（`doc/license/queue.txt`）。R128Scan 由来の部分は MIT（`doc/license/R128Scan.txt`）
- 配布物：含む（ラウドネスの測定に使う。ReplayGain 2.0。`ebur128/ebur128.c` の1ファイルだけを静的ライブラリとして組み込む。ピークの関数は使わない）
- 入手先：https://github.com/jiixyj/libebur128 （ソースは上の配布物。変更なし）
- 著作権表示：
  - Copyright (c) 2011 Jan Kokemüller
  - Copyright (C) 2011 Chris Moeller（R128Scan、Portions copyright (c) 2011 Jan Kokemüller）
  - Copyright (c) 1991, 1993 The Regents of the University of California. All rights reserved.（queue.h）
- ライセンス本文：libebur128 の `COPYING`、`doc/license/R128Scan.txt`、`doc/license/queue.txt`

## SQLite

- バージョン：3.50.4（amalgamation `sqlite-amalgamation-3500400.zip`、SHA-256 `1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032`）
- ライセンス：パブリックドメイン（著作権を放棄している。AGPLv3 と両立する）
- 配布物：含む（ライブラリのデータベース。`sqlite3.c` を FTS5 つきで静的ライブラリとして組み込む。`shell.c` は使わない）
- 入手先：https://sqlite.org/ （ソースは上の配布物。変更なし）
- 著作権表示：なし（ソースの冒頭に「The author disclaims copyright to this source code.」とある）
- ライセンス本文：https://sqlite.org/copyright.html

---

## LAME（テスト専用）

- バージョン：3.100（`lame-3.100.tar.gz`、SHA-256 `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`）
- ライセンス：LGPL-2.0-or-later（GNU Library General Public License）
- 配布物：含まない（テスト用の MP3 ファイルを作るためだけに、テストの中でビルドする）
- 入手先：https://lame.sourceforge.io/
- 著作権表示：Copyrights (c) 1999-2011 by The LAME Project ほか（`README` を参照）

---

## Microsoft WebView2 SDK

- バージョン：1.0.3485.44（NuGet パッケージ `Microsoft.Web.WebView2`）
- ライセンス：BSD-3-Clause
- 配布物：含む（静的ローダー `WebView2LoaderStatic.lib` をアプリにリンク。WebView2 ランタイム本体は Windows に入っているものを使い、同梱しない）
- 入手先：https://www.nuget.org/packages/Microsoft.Web.WebView2
- 著作権表示：Copyright (C) Microsoft Corporation. All rights reserved.
- ライセンス本文：パッケージの `LICENSE.txt`

---

## 画面のビルドとテストにだけ使うもの（配布物に含まない）

`ui/package.json` の devDependencies。Vite が `ui/src/` から作るファイルだけを配布し、これらのパッケージ自体は含まない。

| 名前 | バージョン | ライセンス | 入手先 |
|---|---|---|---|
| Vite | 8.3.6 | MIT | https://github.com/vitejs/vite |
| TypeScript | 7.0.2 | Apache-2.0 | https://github.com/microsoft/TypeScript |
| Vitest | 5.1.0 | MIT | https://github.com/vitest-dev/vitest |

これらが依存するパッケージは `ui/package-lock.json` を参照。

---

## Catch2

- バージョン：3.16.1（コミット `08092139210881f01a34e60c5f2fd6afa0ea4024`）
- ライセンス：BSL-1.0
- 配布物：含まない（テスト専用）
- 入手先：https://github.com/catchorg/Catch2
- 著作権表示：Copyright Catch2 Authors
- ライセンス本文：Catch2 の `LICENSE.txt`
