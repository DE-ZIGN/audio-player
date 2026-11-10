# tools/

開発用のツール。配布物には含まない。

## ap_play（S1 の試作プレイヤー）

WAV・FLAC・AIFF・ALAC（と DSF/DFF を DoP で）を整数のまま、MP3・AAC を 32bit 浮動小数点のまま、排他 WASAPI（イベント駆動）か ASIO で再生する。既定は原音経路で、音量、変換、ディザはかけない。`--volume` で音量を下げたときと、浮動小数点の曲を浮動小数点を受け付けないデバイスで鳴らすときだけ、64bit の処理経路を通して TPDF ディザをかけ、デバイスが受け付けるいちばん細かい整数にする。形式はファイルの中身で判断する。FLAC は最後まで読んだときに MD5 を照合し、終わりに結果を表示する。
ビルドすると `build/vs2022/tools/play/<Debug|Release>/ap_play.exe` ができる。

```
ap_play --list                          出力デバイスの一覧
ap_play --formats [--device N]          デバイスが排他モードで受け付けるレートと形式の一覧
ap_play --watch                         出力デバイスの変化（抜き差し、既定の変更など）を表示する
ap_play FILE... [--device N] [--check]  順に再生（--check は送る形式を確かめるだけ）
ap_play FILE... --exact-only            コンテナの詰め替えもしない
ap_play FILE... --dummy OUT.raw         デバイスに送るはずのバイト列をファイルに書き、全曲をつなげた入力とハッシュを比べる
ap_play FILE... --stress N              N 個のスレッドで CPU に負荷をかけながら再生し、音切れの回数を表示する
ap_play FILE... --inject-stall S:MS     1 曲目の S 秒の時点で再生スレッドを MS ミリ秒止める（音切れの検出の確認用）
ap_play FILE... --dop                   DSD（.dsf/.dff）を DoP で送る。DoP に対応した DAC のときだけ使う（対応していないと大きなノイズになる）
ap_play FILE... --from S [--for S]      1 曲目の S 秒の位置から再生し、--for S なら再生を始めてから S 秒（実時間）で止める（実機での試聴用）
ap_play FILE... --volume DB             デジタル音量（0 以下の dB）。0 なら原音経路のまま、下げると処理経路
ap_play FILE... --volume-at S:DB        再生を始めてから S 秒で音量を DB にする（何回でも。0 に戻すと原音経路に戻り、出力を開き直す）
ap_play --analyze FILE...               サンプルピークと True Peak（8 倍オーバーサンプリング）をチャンネルごとに測り、最大の位置と、0 dBTP を超えるかを表示する。ラウドネス（BS.1770、libebur128）と ReplayGain 2.0 の値も出し、複数のファイルならアルバムとしての値も出す（再生はしない）
ap_play FILE... --replaygain track|album  再生の前に各ファイルを解析し、ReplayGain（-18 LUFS、True Peak が 0 dBTP を超えないように抑える）を処理経路でかける。アルバムは渡したファイル全体
ap_play FILE... --eq PRESET.txt         パラメトリック EQ（AutoEQ の ParametricEQ.txt、Equalizer APO の書き方）。最小位相、プリアンプは自動（最大のブーストを 0 dB に。ファイルの Preamp は表示だけ）
ap_play FILE... --compare original|processed [--compare-at S]...  聴き比べ。原音と音作りの両方を作り、音作り側を原音の音量に合わせる（K 特性のエネルギーの比）。最初にどちらを出すかを決め、--compare-at S で S 秒の時点でもう一方に切り替える（5 ms のクロスフェード）。終わりに、合わせたゲイン、出てくる音の音量差、聴き比べ用のリミッターの記録を出す
ap_play FILE --abx [N] [--eq ...]       ABX（既定 16 回）。キー a/b/x で聴く音を切り替え、1 で「X は A」、2 で「X は B」と答え、q で中断。正解数と p 値は最後にだけ出す（途中で見ると p の意味が崩れるため）。音作り側の音量は、曲全体を同じ設定の別の処理の列で解析して原音に合わせる。コンソールで直接実行する（キーボードで答えるため）。曲が終わる前に答え終わらなければ、--from の位置から流し直す。試聴用の EQ は `tools/play/test-eq.txt`（このプロジェクトで書いたもの。AutoEQ のデータではない）
ap_play FILE... --isp-safe off|measured|fixed  ISP セーフ。measured は解析した True Peak から -0.5 dBTP 以下になるだけ下げる（ReplayGain や音量で下がった分は差し引く）、fixed は常に -3 dB。off でも解析して、0 dBTP を超える曲を原音経路で鳴らすときに警告する。処理経路では保険のリミッター（-0.3 dBTP）が働き、終わりに働いた回数と量を表示する
ap_play --asio-list                     ASIO ドライバーの一覧（各ドライバーを別のプロセスで読み込み、形式と使えるレートを表示）
ap_play FILE... --asio N|NAME           ASIO で順に再生（--asio-list の番号か名前）。レートは曲ごとにドライバーへ設定する
ap_play --env                        実行環境（MSIX パッケージの中かどうか）
ap_play --asio-list                  ASIO ドライバーの一覧（レジストリ）
ap_play --asio-load N|all            ASIO ドライバーを読み込めるか試す（再生はしない）
```

- `--asio-load` は、ドライバーの COM オブジェクトを作って解放するだけ（S2 の確認用、同じプロセスで読み込む）。ドライバーによっては、それだけでアプリごと落ちる（Realtek ASIO で確認。初期化せずに解放した場合）。普段は、別のプロセスで確かめる `--asio-list` を使う。
- MSIX の中で試すときは `packaging/msix-test/README.md` を参照。

- 複数のファイルは順に再生する。形式が同じなら出力を開き直さずにつなげ（ギャップレス）、違えば開き直して、切り替えにかかった時間を表示する。
- デバイスが受け付けない形式は再生せず、理由（レート非対応、ビット深度非対応、他のアプリが使用中など）と、デバイスが今受け付けるレートを表示する。
- 24bit を 32bit コンテナでしか受け付けないデバイスには、コンテナを詰め替えて送る（下位8ビットは 0、値は変わらない）。詰め替えたときは表示する。
- ファイルは専用のスレッドで先に読んでおく（約2秒分。音切れを防ぐため）。終わりに音切れの回数（デバイスへの受け渡しの遅れ、ファイルの読み込みの遅れ、デバイスの応答なし）と、再生スレッドに MMCSS が効いたかを表示する。
- `--analyze` で `ap_signalgen isp` のファイルを測ると、ファイル全体では約 +3.10 dBTP と出る。信号が急に始まって急に終わるので、帯域制限した波形がその端で本当に行き過ぎるため。続いている部分は +3.01 dBTP（受け入れテストで確かめている）。
- **注意**：`bitpattern`、`isp`、`sweep` の信号はフルスケールに近く、とても大きな音になる。スピーカーや耳を傷めるので、デバイスで再生しないこと（ダミー出力で使う）。

## ap_signalgen（テスト信号の生成）

受け入れテスト（CLAUDE.md 5章）や実機確認で使うテスト信号を WAV で書き出す。`--out` の拡張子が `.flac` なら FLAC で書き出す（整数形式のみ。MD5 と 1 秒ごとのシーク表つき。libFLAC でエンコードし、エンコーダー自身の検証を通している）。
ビルドすると `build/vs2022/tools/signalgen/<Debug|Release>/ap_signalgen.exe` ができる。

### 信号

| コマンド | 内容 | 既定値 |
|---|---|---|
| `isp` | fs/4・位相45度のサイン波。サンプル値は +A, +A, −A, −A の繰り返し。サンプル間の波形のピークはサンプルのピークより +3.01 dB 高い（0 dBFS なら +3.01 dBTP） | `--level 0` |
| `sine` | サイン波（`--phase` で開始位相を度で指定） | `--freq 1000 --level -90 --phase 0` |
| `bitpattern` | 下位ビットのパターン（整数形式のみ）。0、各ビットだけ立てた値（正負）、最大・最小、0〜255 と −1〜−256、0x55…/0x2A… とその反転。右チャンネルは左のビット反転（左＋右 = −1） | — |
| `sweep` | 対数スイープ（Farina 方式） | `--start 20 --end 20000 --level -6 --seconds 10` |
| `all` | 上の4種類を、レートと形式の組み合わせすべてで書き出す | 44.1〜384 kHz × 16/24/32bit |
| `dsd` | DSD のサイン波（2次のΔΣ変調、0 dB = 変調度 50%）を `.dsf` か `.dff` で書き出す | `--dsd-rate 2822400 --freq 1000 --level -20 --seconds 5` |

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
