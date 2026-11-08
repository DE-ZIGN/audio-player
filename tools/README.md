# tools/

開発用のツール。配布物には含まない。

## ap_play（S1 の試作プレイヤー）

WAV を整数のまま、排他 WASAPI（イベント駆動）で再生する。音量、変換、ディザはかけない。
ビルドすると `build/vs2022/tools/play/<Debug|Release>/ap_play.exe` ができる。

```
ap_play --list                       出力デバイスの一覧
ap_play --formats [--device N]       デバイスが排他モードで受け付けるレートと形式の一覧
ap_play FILE [--device N] [--check]  再生（--check は送る形式を確かめるだけ）
ap_play FILE --exact-only            コンテナの詰め替えもしない
ap_play FILE --dummy OUT.raw         デバイスに送るはずのバイト列をファイルに書き、入力とハッシュを比べる
```

- デバイスが受け付けない形式は再生せず、理由（レート非対応、ビット深度非対応、他のアプリが使用中など）を表示する。
- 24bit を 32bit コンテナでしか受け付けないデバイスには、コンテナを詰め替えて送る（下位8ビットは 0、値は変わらない）。詰め替えたときは表示する。
- 再生前にファイル全体をメモリーに読む（音切れ防止のため。先読みの仕組みはタスク 1-3 で作る）。
- **注意**：`bitpattern`、`isp`、`sweep` の信号はフルスケールに近く、とても大きな音になる。スピーカーや耳を傷めるので、デバイスで再生しないこと（ダミー出力で使う）。

## ap_signalgen（テスト信号の生成）

受け入れテスト（CLAUDE.md 5章）や実機確認で使うテスト信号を WAV で書き出す。
ビルドすると `build/vs2022/tools/signalgen/<Debug|Release>/ap_signalgen.exe` ができる。

### 信号

| コマンド | 内容 | 既定値 |
|---|---|---|
| `isp` | fs/4・位相45度のサイン波。サンプル値は +A, +A, −A, −A の繰り返し。サンプル間の波形のピークはサンプルのピークより +3.01 dB 高い（0 dBFS なら +3.01 dBTP） | `--level 0` |
| `sine` | サイン波 | `--freq 1000 --level -90` |
| `bitpattern` | 下位ビットのパターン（整数形式のみ）。0、各ビットだけ立てた値（正負）、最大・最小、0〜255 と −1〜−256、0x55…/0x2A… とその反転。右チャンネルは左のビット反転（左＋右 = −1） | — |
| `sweep` | 対数スイープ（Farina 方式） | `--start 20 --end 20000 --level -6 --seconds 10` |
| `all` | 上の4種類を、レートと形式の組み合わせすべてで書き出す | 44.1〜384 kHz × 16/24/32bit |

共通オプション：`--rate`（48000）、`--bits 16|24|32|f32|f64`（24）、`--channels`（2）、`--seconds`（1）

### 約束ごと

- dBFS はピーク基準（ピークがフルスケールのサイン波 = 0 dBFS）。
- 整数のフルスケールは 2^(N−1)−1（16bit なら 32767）。変換は四捨五入（0.5 は0から遠い方へ）で、ディザはかけない。同じ引数なら毎回同じ値になる。
- 16bit で2チャンネル以下は `WAVE_FORMAT_PCM`、それ以外は `WAVE_FORMAT_EXTENSIBLE`。浮動小数点には `fact` チャンクを付ける。

### 例

```
ap_signalgen isp --rate 44100 --bits 16 --out isp.wav
ap_signalgen sine --rate 96000 --bits 24 --freq 1000 --level -90 --out sine.wav
ap_signalgen all --out-dir tools/out
```

`tools/out/` は `.gitignore` 済み。

### テスト

- `tests/unit/signalgen_*_test.cpp`、`tests/unit/wav_writer_test.cpp`：生成コードと WAV 書き出しの単体テスト。
  ISP 信号は、窓付き sinc 補間でサンプル間の値を求め、+3.01 dB（±0.01 dB）になることも確かめている。
- `tests/files/signal_files_test.cpp`：ctest の中で `ap_signalgen all` を実行してファイルを書き出し、
  生成側とは別に書いた WAV 読み込みコード（`tests/support/wav_reader.cpp`）で値を確かめる。
