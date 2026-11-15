# 同梱IRの候補リストと試聴手順（2026-11-06）

（詳細版と試聴メモは作業用の非公開ドキュメント。ここは要約。M9 の 9-6 で使う。2026-11-14 にフェーズ2から前倒し）

**保留中**: 試聴曲がそろうまでIR選定は止めている。再開時は、取得済みZIPのステレオIR化（MAIR=ペア結合、Central Hall=Bフォーマットのデコード。どちらも `tools/ir_prepare.py`。Numen のモノラルはそのまま使える）→ 下の「アプリでの試聴」から始める。
**2026-11-15（9-6）**: アプリに空間の段ができた。内蔵の CLUB・HALL・ARENA は今はすべて合成（`docs/phase1-tasks.md` の 9-6 の記録）。候補の IR は、設定画面の「空間 → IR のファイル」で、ボタンごとに割り当てて試聴できる。
**IRファイルそのものはリポジトリに入れない。** 同梱が決まったCC BYのものだけ、帰属表示とともに追加する。

## 区分と有力候補
- 中（ホール）: MAIR（St. Paul's Hall、RT60 2.1秒、CC BY 4.0。licence.txtで確認）が本命
- 中: Jack Lyons Concert Hall（350席、CC BY 4.0。生成種別「計算モデル」表記は要確認）も有力
- 中・要確認: Central Hall（1,190席）。OpenAIRの公開サイトが停止中でライセンス未確認。同大の他作品はCC BY 4.0なので可能性は高い。復旧待ちか、York AudioLabに問い合わせ
- 中・要確認: Spokane Woman's Club
- 中・参考のみ: Usina del Arte Symphony Hall（ライセンス名の表記なし → 許諾なしでは同梱不可）
- 小・要確認: Numen Free IR Pack（合成20空間、Jazz Club・Live Room等。モノラル24bit、44.1/48/96kHz。ZIP内にライセンス文書なし）、Hendrix Hall、Dixon Studio Theatre（約120席）、Genesis 6 Studio
- 小・参考のみ: Adventure Kid Small Rooms（CC BY 4.0、クレジット必須。シンセ向けに大きく加工した小部屋で、会場の響きではない）
- 大・要確認: Sports Centre（York）、Terry's Factory Warehouse、T2格納庫（Yorkshire Air Museum）
- 音色づけ・参考のみ: Conner's IR Library（MIT、738個・約1GB、著作権表示と許諾文の同梱が必要。中身はスピーカー・スプリング・テープ機・車内などで会場なし）
- OpenAIRのライセンスは各作品ページ最下部の Attribution 欄で確認

## 取得済みファイル（作業フォルダーの「資料」、リポジトリ外）
- Numen Free IR Pack: 20空間×3レート=60WAV、すべてモノラル24bit。ライセンス文書なし
- MAIR-Library-and-Renderer（約900MB）: licence.txt（CC BY 4.0全文）とreadme同梱。使うのは MainArrays/2ch_*（AB・ORTF・XY・KU100）。各マイク別のモノラルWAV（32bit float、44.1kHz）で、FL/FRを組んでステレオ化。音源位置S01〜S13。SADIE_HRIRsは別ライセンスなので同梱しない
- Central Hall（University of York）: Bフォーマット4ch・96kHz・16bit・2.5秒×6位置。ステレオ化には仮想マイクでのデコードが必要。ライセンス文書なし

## 試聴手順
1. OpenAIRの畳み込み例（声・ドラム）で一次選考、ライセンス確認
2. 候補ZIPを作業フォルダーの「資料」に保存
3. 試聴曲4曲（女性ボーカル、アコースティック・ジャズ、オーケストラ、ロック）各30秒
4. 響きの量・プリディレイなし・音量を揃えて比較
5. 自然さ、低域の濁り、距離感、ノイズ、合うジャンルをメモ
- 比較音源の一括書き出しスクリプトは Claude Code で作れる

## アプリでの試聴（2026-11-15 から）
1. 候補をステレオの WAV にする：
   - MAIR：`python tools/ir_prepare.py pair <FL.wav> <FR.wav> <出力.wav>`（同じ音源位置・同じマイク方式の 2 本）
   - Central Hall：`python tools/ir_prepare.py bformat <Bフォーマット.wav> <出力.wav> --order fuma`（チャンネルの並びが AmbiX なら `--order ambix`。2 本のカーディオイドの開きは `--angle`、既定 90 度）
   - `python tools/ir_prepare.py info <ファイル>` で長さとピークを確かめる
   - 直接音の前に雑音や、ゆっくりした立ち上がりがあるもの（Central Hall は約 −48 dB、Numen は −55〜−45 dB の立ち上がり）は、`--trim-db -40` で直接音の 1 ms 前から使う（ステレオのものは `trim`）。アプリは −80 dB 未満しか削らないので、そのままだと響きの始まりが IR ごとにずれる
   - 2026-11-15：試聴用の 11 本（MAIR 2・Central Hall 2・Numen 7）を作業フォルダーの `資料/IR試聴/` に作った。組み合わせと測定値（帯域ごとの T20、左右の相関）は同じフォルダーの README.md。Numen の 48 kHz 版は、左右の違うステレオだった
2. アプリを音作りモードにし、設定画面の「空間 → IR のファイル」で、比べたい 2〜3 個を CLUB・HALL・ARENA のボタンに割り当てる（読めた長さ・チャンネル・レートが出る）
3. 試聴曲を再生し、アンプビューの SPACE のボタンで切り替えて聴く（切り替えは約 40 ms のクロスフェード、途切れない）。MIX・プリディレイ・低域カットは同じにそろえる（響きの音量は IR ごとにエネルギーでそろえてある）
4. 上の「試聴手順」の 5 の観点でメモし、内蔵の合成の部屋（「内蔵に戻す」）とも比べる
5. 決めたものは、ライセンス（CC BY 系か、帰属表示の文言）を確かめてから同梱の作業へ

## 自前生成
- 小: pyroomacoustics（鏡像法＋音線追跡、ランダム化ISM）で箱型の部屋を計算
- 大: まばらな遅い反射＋薄い残響＋高域の空気減衰を設計してIRを組み立て、反射量を調整可能に
