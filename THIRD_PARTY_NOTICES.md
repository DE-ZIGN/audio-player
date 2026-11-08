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
- 使っているモジュール：`juce_core`、`juce_events`、`juce_graphics`、`juce_gui_basics`

以下は、上記のモジュールに同梱されていてアプリに組み込まれる部品です（JUCE の `LICENSE.md` の一覧による）。

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

## Catch2

- バージョン：3.16.1（コミット `08092139210881f01a34e60c5f2fd6afa0ea4024`）
- ライセンス：BSL-1.0
- 配布物：含まない（テスト専用）
- 入手先：https://github.com/catchorg/Catch2
- 著作権表示：Copyright Catch2 Authors
- ライセンス本文：Catch2 の `LICENSE.txt`
