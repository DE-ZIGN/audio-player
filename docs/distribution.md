# 配布と掲載の方針（2026-11-22）

掲載先は 2026-11-22 に調べた。掲載依頼はまだしていない（依頼文も後で書く）。送る前に、各サイトの条件を読み直す。

## 方針

1. **配布元は Microsoft Store だけ**（初回。2026-11-22 に決定）。サイトにも掲載先にも、exe・ZIP・MSIX は渡さない。
2. **ほかのサイトには、Store へのリンクで載せてもらう。** ファイルのアップロードや転載が条件の所には、今は出さない。
3. **入口はプロダクトサイト**（https://audio.dezign.jp/）。掲載先には紹介ページとしてサイトを、ダウンロード先として Store の URL を渡す。
4. **依頼は公開（Publish now）の後。** 審査中や公開前には送らない。送信は、文面と宛先を U が確かめてから。
5. **紹介文は Store の掲載文（`packaging/store/listing.json`）の範囲を超えない。** 条件（音量 100%、同じ形式どうしのギャップレス、DoP の注意など）を落とさない。
6. GitHub Releases は署名の見通しが立ってから（`monetization.md`）。出すことにしたら、この方針を見直す。

## 進める順

| 段 | いつ | やること |
|---|---|---|
| 1 | 公開の直後 | Store の URL と、`winget` の msstore から入るかを確かめる。サイトに公式バッジ（Launch mode は Direct）、README にリンク |
| 2 | 依頼の前 | 下の「依頼の前に要るもの」をそろえる |
| 3 | 日本 | 窓の杜、フリーソフト100 |
| 4 | 海外 | AlternativeTo、FossHub |
| 5 | 反応を見て | 下の「保留」。フォーラムは宣伝の規則を読んでから |

## 掲載先

根拠の強さ：**実例**＝Store リンクで載っているページを読んだ／**原文**＝公式の案内を読んだ／**検索**＝検索結果の要約だけで、原文は未確認。

### 出す

| 掲載先 | Store リンク | 方法 | 根拠 |
|---|---|---|---|
| 窓の杜 | 可。ボタンが Store だけの例（[iTunes](https://forest.watch.impress.co.jp/library/software/itunes/)）もある | [編集部へメールで自薦](https://forest.watch.impress.co.jp/info/sakusya/sakusya3.html)。まず記事として検討、原則返事なし。ファイルは添付しない | 実例・原文 |
| フリーソフト100 | 可（[VLC](https://freesoft-100.com/review/vlc-media-player.php) など。Store だけの例は未確認） | [掲載リクエストのフォーム](https://freesoft-100.com/about/contact.html)。自薦可、掲載の保証なし | 実例・原文 |
| AlternativeTo | 可の見込み | [アカウントを作って申請](https://alternativeto.net/faq)。未公開のものは却下されるとの情報 | 検索 |
| FossHub | 可の見込み（公式へのリンク集になった） | [問い合わせフォーム](https://www.fosshub.com/contact.html)で、公式サイトを添えて推薦（[FAQ](https://www.fosshub.com/faq.html)） | 原文 |

窓の杜の[収録の説明](https://forest.watch.impress.co.jp/info/sakusya/sakusya1.html)（2018 年）には「原則はファイルの転載」とあり、ライブラリへの収録は編集部が選ぶ。Store だけで収録される保証はない。

### 保留

| 掲載先 | 理由 | 根拠 |
|---|---|---|
| k本的に無料ソフト | Store リンクの[例](https://www.gigafree.net/media/music/foobar2000.html)はあるが、投稿窓口が見つからない | 実例 |
| Product Hunt | 製品ページ＋Store リンクで[出せる](https://help.producthunt.com/posting/how-to-post-a-product)が、客層が違う | 検索 |
| Softpedia | Store アプリの掲載例はあるが、ダウンロードの方式が不明。要問い合わせ | 検索 |
| MajorGeeks | Store だけの掲載例も、投稿の条件も見つからない | 未確認 |
| [awesome-windows](https://github.com/0PandaDEV/awesome-windows/blob/main/CONTRIBUTING.md) | GitHub の Star が 50 以上必要 | 原文 |
| Hydrogenaudio、Audio Science Review | 客層は合うが、宣伝の規則が未確認 | 未確認 |

### 出さない（Store だけの間）

| 掲載先 | 理由 | 根拠 |
|---|---|---|
| Vector | [ファイル本体のアップロードが必須](https://www.vector.co.jp/info/web_form/webform_info.html) | 原文 |
| SourceForge | [ファイルの配布](https://sourceforge.net/create)が前提 | 原文 |
| Softonic | 開発者フォームがプログラムのファイルを求める | 検索 |

## 依頼の前に要るもの

- [ ] Store で公開済みであることと、その URL
- [ ] サイトに版、公開日、更新履歴（窓の杜の自薦に版と公開日が要る）
- [ ] サイトに「公式の配布は Microsoft Store だけ」の明記（AGPL なので第三者の再配布は止められない。公式がどこかを示す）
- [ ] 英語の紹介ページ（海外に出す場合。今のサイトは日本語だけ）
- [ ] 依頼文（日本語・英語）
- [ ] 掲載先ごとのリンク：`https://apps.microsoft.com/detail/9NN52LJ5BCCF?cid=<掲載先>`（流入元の区別）

## 公式のインストール方法

- [Store のバッジ](https://apps.microsoft.com/badge)：Launch mode を Direct にすると、Store の画面を経ずに[小さな公式インストーラー](https://learn.microsoft.com/en-us/windows/apps/distribute-through-store/how-to-use-store-web-installer-for-distribution)で入る（無料の MSIX が対象）。サイトにファイルを置かずに済む。
- winget：[msstore が標準の取得元](https://learn.microsoft.com/en-us/windows/package-manager/winget/source)。`winget install 9NN52LJ5BCCF -s msstore` で入る見込み（公開後に確かめるまで案内しない）。
