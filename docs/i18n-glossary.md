# 英語の用語集（タスク 11-4）

画面の英語（`ui/src/i18n/`）で、同じ語をいつも同じ訳にするための表。訳文でも CLAUDE.md 1 章の方針（「音が良くなる」と断定しない。何をしているかと測定値を書く）を守る。

- 書き方：アメリカ英語。見出し・ボタン・タブは先頭だけ大文字（Sentence case、例 "Output rate"）。アンプのパネルの印字（DRIVE、BASS など）は今のまま大文字。
- 数と単位：今と同じ（`44.1 kHz`、`−3.0 dB`、`24bit` は `24-bit` にする）。
- 状態：「決定」はユーザーが決めた語。「案」は Claude の案で、ユーザーが「案のままでよい」とした（2026-11-20）。画面で英語を見てから、気になる語は直す。

## 製品の考え方にかかわる語

| 日本語 | 英語 | 状態 | メモ |
|---|---|---|---|
| 原音モード | Pure mode | 決定（2026-11-20） | アンプの MODE の PURE と同じ語 |
| 音作りモード | Processing mode | 決定（2026-11-20） | ReplayGain・EQ・真空管・空間などをかける。アンプの TUBE とは別（真空管だけではないため） |
| 原音経路 | Direct path | 案 | 何も処理しない経路 |
| 処理経路 | Processing path | 案 | 64bit 浮動小数点の処理をする経路 |
| 出力の確認 | Signal path | 案 | 画面の題。CLAUDE.md 8 章の「SIGNAL PATH」 |
| ロマン枠（効果は確かめていません） | Just for fun: no effect verified | 決定（2026-11-20） | 効果を主張しない語にする |
| 演出 | Simulation | 案 | ウォームアップ・個体差の見出し |
| 聴き比べる（ABX） | Compare blind (ABX) | 案 | 「ABX で聴き比べる…」は "Compare blind with ABX…" |

## そのまま使う語（訳さない）

BIT-PERFECT、True Peak、dBTP、LUFS、ABX、ReplayGain、EQ、DoP、DSD、WASAPI、ASIO、TPDF、IR、MIX、DRIVE、BASS、TREBLE、VOLUME、SPACE、CROSSFEED、TUBE TYPE、PURE、TUBE、OFF・CLUB・HALL・ARENA、SE 300B・PP EL34・PP CLASS A、AUTO。

## よく出る語

| 日本語 | 英語 |
|---|---|
| 経路 | path |
| 真空管 | tube（設定のタブは Tubes） |
| 球 | tube |
| 球の番号 | tube number |
| トーン | tone |
| 空間 | space（部屋の響きの畳み込み）。部屋は room |
| クロスフィード | crossfeed |
| プリアンプ | preamp |
| ISP セーフ | ISP-safe |
| 保険のリミッター | safety limiter |
| ディザ | dither |
| 排他モード | exclusive mode |
| 既定のデバイス | default device |
| 出力デバイス | output device |
| 出力レート | output rate |
| 曲のまま・固定・下限・整数倍 | As the track / Fixed / At least / Integer multiple |
| ウォームアップ | warm-up |
| 個体差 | tube-to-tube variation |
| 合成・実測・自分のファイル（IR） | synthetic / measured / your own file |
| プリディレイ・ローカット | pre-delay / low cut |
| 区間・回数・正解（ABX） | segment / trials / correct |
| 詰め替え | repacking |
| 自己確認 | self-check |
| 音切れ | dropouts |
| 測定待ち | waiting to be measured |
| 監視フォルダー | watched folders |
| 再生キュー | queue |
| ジャンルの対応表・アルバムごとの指定 | genre table / per-album choice |
| ラウドネス（統合・短時間・瞬時） | loudness (integrated / short-term / momentary) |
| 標準ビュー・アンプビュー・ライブラリビュー | Standard view / Amp view / Library view |
| 詳細 | Details |
| 模擬 | mock |

## 訳すときに足した語（11-4、2026-11-20）

| 日本語 | 英語 |
|---|---|
| 見た目（設定の見出し・タブ） | Appearance（見出し）、Look（タブ。アンプビューで 1 段に収めるため） |
| 〜にする（ビューの切り替え） | Standard view / Amp view / Library view（ボタンに収めるため、動詞を付けない） |
| ABX の比べるもの | Pure vs. processed (current settings) / Pure vs. 16-bit (TPDF dither) / Settings A vs. settings B |
| ABX の A・B の側 | Pure / Processed / Settings A / Settings B |
| 起動直後に固定（ABX） | Cold, as at launch |
| 回目（ABX） | Trial {n} of {m} |
| 音量差 | level difference |
| 自前（デコーダー） | built-in |
| 照合（MD5） | check（MD5 match / MD5 mismatch） |
| 浮動小数点 | float（"32-bit float"） |
| 伝達特性・電源の沈み込み | transfer curve / power-supply sag |
| 区切った FFT による畳み込み | partitioned FFT convolution |
| 処理の遅れ | processing latency |
| クロスフィードの強さ（標準・やや弱め・弱め） | Standard / Slightly weaker / Weaker |
| ISP セーフの選び方（計測した値で下げる・固定で −3 dB） | Lower by the measured value / Fixed −3 dB |
| 〜順（並べ替え）・新しい順 | By artist など / (newest first) |
| 数（曲数、行数など） | 名札の形（"Tracks: {n}"、括弧の中では小文字 "rows: {n}"）。`t()` に複数形の仕組みがなく、"1 tracks" とならないように |
| どちらの音がよいかは判定しません | It does not judge which of the two is preferable. |

