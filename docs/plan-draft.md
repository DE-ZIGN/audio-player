# 企画書（初稿）2026-11-08

（詳細版は作業用の非公開ドキュメント。ここは要約）

> 2026-11-08 決定: 収益は寄付のみ。ソースを公開する（AGPLv3）。配布は Microsoft Store が主、GitHub 版は SignPath で署名。詳細は `monetization.md`。

## 構成
概要／背景と狙い（5系統と市場の空白）／設計の柱（原音経路・音作り経路・検証の図）／対象ユーザー／主な機能（分野×フェーズ）／競合との比較（精査済み、2026-11-08）／技術構成／フェーズ計画／配布と収益・ライセンスの制約／UI（未定）／リスクと未決事項／関連資料

## 要点
- Windows向け無料の高音質プレイヤー。原音経路（処理なし、ビットパーフェクト）と音作り経路（ReplayGain→EQ・真空管・空間→リサンプラー→ISPゲイン→リミッター（保険）→TPDFディザ）を分け、ABX・経路表示・メーターで検証
- 対象: PCオーディオ好き（中心）、音作り派、科学派、日本のユーザー。当面Mac/Linux・ストリーミング中心は対象外
- フェーズ1: 原音プレイヤー＋検証＋ライブラリ基本＋記事5本（完了の目安＝受け入れ基準の自動テスト）。2: 音作り・リサンプラー・VST3・ASIOネイティブDSD。3: 回路モデル等とロマン枠。後半: ストリーミング
- 収益: 寄付のみ（寄付の有無で機能は同じ）

## 競合比較の精査結果（2026-11-08）
比較表の列: 本企画／HQPlayer／Audirvāna／Roon／JRiver／foobar2000／TuneBrowser／EffeTune。PGGBは変換ツールなので表外（`pggb-dsd-verification.md` で扱う）。

価格
- HQPlayer: 公式ページに価格表示なし（フォーラムでv4に約300ポンドの報告）
- Audirvāna: Studio 月7.99ユーロ／年79.99ユーロ（2026-01-06から）、Origin買い切り（149.99ドルと119.99ドルの記載が混在）
- Roon: 月14.99ドルから、ライフタイム829.99ドル
- JRiver: Windows版99.98ドル、全OS版（Master）124.98ドル、Linux版79.98ドル
- TuneBrowser: 無料版は500曲まで、Full版は買い切り（機能差なし）
- foobar2000、EffeTune（MIT）は無料

ISP・クリップ対策（各社とも手当て済み → 差別化にならない）
- HQPlayer: ライブラリでTP・RMS・ラウドネス解析、RG 2.0、クリップ音源向け smooth level limiter
- Roon: MUSEのHeadroom Management、1サンプルでも反応するクリップ表示、インターサンプルオーバーを挙げて−3 dBを推奨、R128解析でdBTPを記録
- JRiver: Clip Protection（既定）でインターサンプルも含めて音量を下げる、R128＋Volume Levelingで回避、Audio Pathに変更を表示
- Audirvāna: RGとピーク値から自動で減衰、ほかの曲向けに予防減衰を設定可。ユーザーから「RGはSRCの後にかかる」との指摘あり
- foobar2000: 「apply gain and prevent clipping according to peak」（RGのピーク値ベース）
- EffeTune: 最終段にブリックウォールリミッター

音作り（標準搭載）
- EffeTune: Tube Simulator（EL84、KT88、300B）、Multiband Saturation、RS Reverb、Room EQ（位相補正、残響低減）、Crossfeed、Loudness EQ、ABX／A/B。110種以上、無料、MIT（DSPライブラリもMIT）。排他・ビットパーフェクトの記載なし、ライブラリは簡易
- Roon: 真空管系なし（フォーラムで要望段階）。クロスフィード、PEQ、畳み込み
- HQPlayer: クロスフィード、PEQ（最小・線形位相）、畳み込み、適応ラウドネス
- TuneBrowser: v5からVST3対応（音作りは外部プラグイン頼み）

日本語
- TuneBrowser: 日本の個人開発、Unicode完全対応、クラシック向け分類、ASIO/WASAPIでDSDネイティブ再生、RAMDecode。Shift_JIS対応は未確認
- 本企画の差を出す余地: 古いShift_JIS ID3タグの自動判定、ひらがな・カタカナ・全角半角をまたぐ検索

差別化の言い直し（単独機能ではなく組み合わせ）
1. ビットパーフェクトの原音経路と、真空管・実測IRの音作り経路を1つのアプリで分けて持つ
2. 何を変えたかを経路表示とTP解析で見せる
3. 効果をABXで本人が確かめられる
4. 無料、日本語前提

直接の競合は無料勢（foobar2000、TuneBrowser無料版、EffeTune）。有料勢（年80ユーロ〜約830ドル）に無料で張り合う構図は維持。

残る要確認: TuneBrowserのクリップ対策・検証機能・ストリーミング、Audirvāna Originの正確な価格、HQPlayerの現行価格

## 未決事項
製品名／UI／同梱IR（試聴曲待ち）／NAS共有時のDB同期／EffeTune DSPライブラリ（MIT）を真空管モデルの参考・流用候補にするか
