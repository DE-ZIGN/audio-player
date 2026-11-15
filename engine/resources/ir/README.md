# 同梱する IR（task 9-6）

エンジンに埋め込む IR（`engine/CMakeLists.txt`、`cmake/EmbedFile.cmake`）。空間の段の「内蔵（実測）」として使う。

## mair_st_pauls_ab_s01.wav（HALL の既定）

- 内容：St Paul's Hall（英国、ハダースフィールド大学のコンサートホール、公表されている残響時間 2.1 秒）で測ったインパルス応答。ステージ正面中央のスピーカー（音源 S01）を、AB（間隔をあけた無指向性マイク 2 本）で録ったもの。
- 出典：MAIR（Microphone Array Impulse Response Library）、Applied Psychoacoustics Lab, University of Huddersfield。作者 Hyunkook Lee、Connor Millns。https://github.com/APL-Huddersfield/MAIR-Library-and-Renderer
  - 文献：Lee, H. and Millns, C. (2017), "Microphone Array Impulse Response (MAIR) Library for Spatial Audio Research," 143rd Audio Engineering Society Convention, New York, e-Brief 356.
- ライセンス：CC BY 4.0（Creative Commons Attribution 4.0 International、https://creativecommons.org/licenses/by/4.0/ 。ライブラリ同梱の `licence.txt` で確認）。
- 元のファイル：`MainArrays/2ch_AB/Main_AB_M01_S01_FL.wav`（左）、`Main_AB_M02_S01_FR.wav`（右）。どちらもモノラル、32bit 浮動小数点、44.1 kHz、3.00 秒。
- 変えたこと（`tools/ir_prepare.py pair … --trim-db -40`）：
  1. 2 本のモノラルを、左・右のステレオ 1 本にまとめた。
  2. 直接音（ピークから −40 dB を超える最初のサンプル）の 1 ms 前より前を切り、その 1 ms をなめらかに立ち上げた（音源からマイクまでの約 36 ms と、その間の雑音を除くため）。
  3. 値は変えていない（32bit 浮動小数点のまま。音量の正規化はアプリが再生時に行う）。
- 結果：44.1 kHz、ステレオ、32bit 浮動小数点、2.965 秒、ピーク −4.6 dBFS。250 Hz〜2 kHz の T20 は 2.13 秒（自動テストで測る）。SHA-256 `7e38ef3bf40561be6c2398d448db8c812635c5898f56fd3a44cc9188c8002af3`。
- 選んだ経緯：`docs/ir-candidates.md`（ユーザーが 11 本の候補と合成の部屋を試聴して選んだ、2026-11-15）。
- アプリの中の表示：設定画面の「空間 → 部屋ごとの IR」に、作者・出典・ライセンス・変えたことを書いている（`ui/src/settings.ts` の `SPACE_MEASURED`）。ライセンス画面（8-2）にも `THIRD_PARTY_NOTICES.md` から載る。
