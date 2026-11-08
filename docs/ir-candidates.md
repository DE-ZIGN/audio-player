# 同梱IRの候補リストと試聴手順（2026-11-06）

（詳細版と試聴メモは作業用の非公開ドキュメント。ここは要約。フェーズ2で使う）

**保留中**: 試聴曲がそろうまでIR選定は止めている。再開時は、取得済みZIPのステレオIR化（MAIR=ペア結合、Central Hall=Bフォーマットのデコード、Numen=擬似ステレオ化）→ 試聴曲4曲×全IRの比較音源書き出し、から始める。
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

## 自前生成
- 小: pyroomacoustics（鏡像法＋音線追跡、ランダム化ISM）で箱型の部屋を計算
- 大: まばらな遅い反射＋薄い残響＋高域の空気減衰を設計してIRを組み立て、反射量を調整可能に
