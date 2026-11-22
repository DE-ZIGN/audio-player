"""The guide for submitting to the Microsoft Store (task 8-5e).

    python tools/make_submission_guide.py

Writes build/store/submission-guide.html: Partner Center's screens in their
order, what to press and choose on each, and every text to paste with a Copy
button. The texts come from packaging/store/ (listing.json, the block quotes
of README.md, privacy-policy.md), so nothing is written twice: change them
there and run this again.
"""

from __future__ import annotations

import html
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STORE = ROOT / "packaging" / "store"
OUT = ROOT / "build" / "store" / "submission-guide.html"

LISTING = json.loads((STORE / "listing.json").read_text(encoding="utf-8"))
README = (STORE / "README.md").read_text(encoding="utf-8")
POLICY = (STORE / "privacy-policy.md").read_text(encoding="utf-8")
VERSION = re.search(r"project\(audio_player\s+VERSION (\d+\.\d+\.\d+)",
                    (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)

esc = html.escape


def quoted(heading: str) -> str:
    part = next(p for p in re.split(r"^### ", README, flags=re.M) if p.startswith(heading))
    return "\n".join(re.sub(r"^> ?", "", line) for line in part.splitlines() if line.startswith(">"))


def field(label: str, value: str, note: str = "") -> str:
    """A text to paste, with its Copy button."""
    rows = min(14, max(1, sum(1 + len(line) // 70 for line in value.split("\n"))))
    said = f'<p class="note">{note}</p>' if note else ""
    return (f'<div class="field"><div class="label"><b>{esc(label)}</b>'
            f'<button type="button" class="copy">コピー</button></div>{said}'
            f'<textarea readonly rows="{rows}" aria-label="{esc(label)}">{esc(value)}</textarea></div>')


def choices(*rows: tuple[str, str]) -> str:
    body = "".join(f"<tr><th>{esc(k)}</th><td>{v}</td></tr>" for k, v in rows)
    return f"<table>{body}</table>"


def step(key: str, title: str, body: str) -> str:
    return (f'<section id="{key}"><h2>{title}</h2>{body}'
            f'<label class="done"><input type="checkbox" data-step="{key}"> この画面は終わった（Save を押した）</label></section>')


def listing(lang: str, name: str) -> str:
    one = LISTING[lang]
    lines = lambda key: "\n".join(one[key])
    out = [f"<h3>{name}（{lang}）</h3>",
           choices(("Product name", f"一覧から <b>{esc(one['productName'])}</b> を選ぶ"),
                   ("What's new in this version", "空のまま（最初の版なので）")),
           field("Description（説明）", lines("description")),
           "<h4>Product features（機能）：1 つの欄に 1 つずつ</h4>"]
    out += [field(f"機能 {i}", v) for i, v in enumerate(one["features"], 1)]
    shots = STORE / "screenshots" / lang
    out += ["<h4>Screenshots（画面写真。Desktop）</h4>",
            "<p>「Browse your files」を押し、次のフォルダーの 5 枚を番号の順に上げます。</p>",
            field("画面写真のフォルダー", str(shots))]
    files = sorted(p.name for p in shots.glob("*.png"))
    out += [field(f"{file} の Caption（字幕）", text) for file, text in zip(files, one["screenshotCaptions"], strict=True)]
    out += ["<h4>Store logos</h4>",
            field("1:1 App tile icon（300 x 300）に上げるファイル", str(STORE / "logos" / "app-tile-300.png"),
                  "ほかのロゴの欄（Poster、Box art）、動画、Xbox の欄は空のままで構いません。"),
            "<h4>Supplemental fields・Additional information</h4>",
            field("Short title", one["shortTitle"]),
            field("Short description", lines("shortDescription")),
            "<p>Sort title、Voice title、Additional system requirements は空のままです。</p>",
            "<h4>Search terms / Keywords（検索語）：1 つの欄に 1 つずつ</h4>"]
    out += [field(f"検索語 {i}", v) for i, v in enumerate(one["searchTerms"], 1)]
    out += [field("Copyright and trademark info", one["copyrightAndTrademark"]),
            field("Additional license terms", lines("additionalLicenseTerms")),
            field("Developed by", one["developedBy"])]
    return "".join(out)


def guide() -> str:
    msix = ROOT / "build" / "msix" / f"DEZIGNAudioPlayer-{VERSION}.0-x64.msix"
    ja_policy, en_policy = POLICY.split('<a id="english"></a>', 1)

    steps = [
        step("s0", "0. 入力の前に：プライバシーポリシーを差し替える", "".join([
            "<p>規約 10.5.1 に合わせて文面に足しました（保存するもの、確認と削除、保護、日付）。"
            "<b>サイトのページを、下の文面に直してから</b>提出します。見出しや箇条書きの形は、サイトに合わせて構いません。</p>",
            field("日本語", ja_policy.replace("[English below](#english)", "").strip()),
            field("英語", en_policy.strip()),
        ])),
        step("s1", "1. 提出を始める", "".join([
            "<ol><li><a href='https://partner.microsoft.com/dashboard' target='_blank' rel='noreferrer'>Partner Center</a> にサインインする。</li>"
            "<li>「Apps and games（アプリとゲーム）」→「DE:ZIGN AUDIO PLAYER」を開く。</li>"
            "<li>Product release の <b>Start submission（申請を開始）</b>を押す。</li></ol>",
            "<p>6 つの項目が並びます。上から順に進めます。<b>Packages を Store listings より先に</b>済ませると、掲載文の言語（日本語・英語）が自動で出ます。</p>",
        ])),
        step("s2", "2. Pricing and availability（価格と提供）", choices(
            ("Markets（市場）", "そのまま（All possible markets）"),
            ("Audience（対象）", "そのまま（Public audience）"),
            ("Discoverability（見つけやすさ）", "そのまま（available and discoverable）"),
            ("Schedule（予定）", "そのまま。公開を待たせる設定は 7 で選びます"),
            ("Base price（基本価格）", "<b>Free（無料）</b>を選ぶ"),
            ("Free trial（試用）", "そのまま（No free trial）"),
            ("Sale pricing、Organizational licensing", "そのまま"),
        ) + "<p>最後に <b>Save</b>。</p>"),
        step("s3", "3. Properties（プロパティ）", "".join([
            choices(("Category（カテゴリ）", "<b>Music（音楽）</b>。サブカテゴリと 2 つ目のカテゴリは選ばない"),
                    ("個人情報にアクセス・収集・送信するか", "<b>Yes（はい）</b>。集めないし送りませんが、規約 10.5.1 が"
                     "デスクトップアプリを「個人情報に触れ得るもの」として扱うためです")),
            field("Privacy policy URL", "https://audio.dezign.jp/privacy/"),
            field("Website", "https://audio.dezign.jp/"),
            field("Support contact info", "https://github.com/DE-ZIGN/audio-player/issues"),
            choices(
                ("電話番号・住所（Contact details）", "個人アカウントでは任意です。空のままにします。必須の印が出たら、入力せずに画面写真を送ってください"),
                ("Display mode", "すべてチェックしない"),
                ("Product declarations：別のドライブに入れられる", "チェックしたまま"),
                ("Product declarations：OneDrive のバックアップ", "チェックしたまま"),
                ("Product declarations：Store の外の決済、アクセシビリティ試験済み、生成 AI、ペン", "チェックしない"),
                ("System requirements", "空のまま"),
            ),
            "<p>最後に <b>Save</b>。</p>",
        ])),
        step("s4", "4. Age ratings（年齢区分）", "".join([
            "<p>最初に種類を聞かれたら、<b>ゲームでも SNS でもない、その他のアプリ</b>に当たるものを選びます。質問は最初の答えで変わります。</p>",
            choices(("暴力、性的な内容、乱暴な言葉、薬物、賭博、恐怖", "すべて <b>No</b>"),
                    ("利用者どうしのやり取り、内容の共有", "<b>No</b>"),
                    ("位置情報の共有", "<b>No</b>"),
                    ("個人情報の第三者への提供", "<b>No</b>（3 の「はい」とは別の質問です。送らないので No）"),
                    ("デジタル商品の購入", "<b>No</b>"),
                    ("制限のないインターネット（ブラウザー機能）", "<b>No</b>")),
            "<p>最後に <b>Save and generate</b>。3+ 相当になる見込みです。発行者名とメールアドレスが年齢区分の機関（IARC）に渡ります。</p>"
            "<p class='warn'>迷う質問が出たら、答えずに画面写真を送ってください。</p>",
        ])),
        step("s5", "5. Packages（パッケージ）", "".join([
            field("上げるファイル", str(msix), "「Browse your files」を押し、出た窓の「ファイル名」に貼ると選べます。"),
            choices(("検証", "<b>Validated</b> になるまで待つ。赤いエラーが出たら画面写真を送ってください"),
                    ("Device family availability", "<b>Windows 10/11 Desktop だけ</b>にチェック。"
                     "「将来のデバイスに Microsoft が提供してよい」はそのまま")),
            "<p>最後に <b>Save</b>。</p>",
        ])),
        step("s6", "6. Store listings（ストア登録情報）", "".join([
            "<p>日本語と英語の <b>両方</b>に入れます。言語が出ていなければ「Manage additional languages」から足します。</p>",
            listing("ja-JP", "日本語"), "<p>ここで <b>Save</b> してから、英語へ。</p>",
            listing("en-US", "英語"), "<p>最後に <b>Save</b>。</p>",
        ])),
        step("s7", "7. Submission options（申請オプション）", "".join([
            choices(("Publishing hold options（公開の保留）",
                     "<b>Don't publish this submission until I select Publish now</b>"
                     "（審査に通っても、自分で押すまで公開しない）")),
            field("Notes for certification（審査への注記）", quoted("審査の人への注記")),
            field("Restricted capabilities：runFullTrust の理由", quoted("制限付きの機能")),
            "<p>最後に <b>Save</b>。</p>",
        ])),
        step("s8", "8. 提出する", "".join([
            "<p class='warn'><b>押す前に</b>、6 つの項目が完了になっている画面の写真を送ってください。一緒に確かめてから押します。</p>"
            "<p>そのあと <b>Submit for certification（または Submit to the Store）</b>を押します。</p>",
            choices(("審査", "最長 3 営業日ほど。Microsoft からのメールに、メールアドレスの確認の依頼が来たら応じる"),
                    ("不合格", "報告書の画面写真を送ってください。直して出し直します"),
                    ("合格", "Store のページを確かめてから <b>Publish now</b> を押すと公開されます。公開が始まると取り消せません")),
        ])),
    ]

    return TEMPLATE.replace("{{VERSION}}", esc(VERSION)).replace("{{STEPS}}", "\n".join(steps))


TEMPLATE = """<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Store 提出の手順</title>
<style>
:root { --bg:#f6f3ee; --panel:#fff; --text:#241f1a; --muted:#6b6258; --line:#d9d1c6; --accent:#95530a; --warn:#fff0d6; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#161310; --panel:#211d19; --text:#eee6dc; --muted:#a89d90; --line:#3b342d; --accent:#eca64f; --warn:#3a2b18; }
}
* { box-sizing:border-box }
body { margin:0; padding:24px 16px 80px; background:var(--bg); color:var(--text); font:15px/1.8 "Yu Gothic UI","Meiryo",sans-serif }
main { max-width:900px; margin:auto }
h1 { font-size:24px; margin:0 } h2 { font-size:19px; margin:0 0 8px; color:var(--accent) }
h3 { border-bottom:2px solid var(--accent); margin:28px 0 8px } h4 { margin:22px 0 4px }
a { color:var(--accent) }
section { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:18px; margin:18px 0 }
table { border-collapse:collapse; width:100%; margin:8px 0 }
th, td { border-top:1px solid var(--line); padding:7px 8px; text-align:left; vertical-align:top; overflow-wrap:anywhere }
th { width:40%; font-weight:normal }
.field { margin:12px 0 } .label { display:flex; justify-content:space-between; align-items:center; gap:8px }
.note { margin:2px 0; color:var(--muted) }
textarea { width:100%; margin-top:4px; padding:7px; border:1px solid var(--line); border-radius:4px; background:var(--bg); color:var(--text); font:13px/1.6 inherit; resize:vertical }
button { flex:none; padding:3px 18px; border:1px solid var(--accent); border-radius:4px; background:transparent; color:var(--accent); cursor:pointer; font:inherit }
.warn { background:var(--warn); padding:8px 10px; border-radius:4px }
.warn::before { content:"\\25B2  "; color:var(--accent) }
.done { display:block; margin-top:14px; color:var(--muted) }
</style></head>
<body><main>
<h1>Microsoft Store への提出：DE:ZIGN AUDIO PLAYER {{VERSION}}</h1>
<p>上から順に進めます。「コピー」を押して、Partner Center の同じ名前の欄に貼ってください。途中でやめても、各画面で Save すれば続きから再開できます。</p>
<p>画面の項目名は公式の説明の英語で書き、日本語は目安として添えています。日本語の画面では訳が少し違うことがあります。</p>
<p class="warn">ここにない質問、必須の印、赤いエラーが出たら、入力せずに画面写真を送ってください。</p>
{{STEPS}}
<p>出典（2026-11-22 に確認）：
<a href="https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/create-app-submission" target="_blank" rel="noreferrer">提出の作り方</a>、
<a href="https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements" target="_blank" rel="noreferrer">パッケージの条件</a>、
<a href="https://learn.microsoft.com/en-us/windows/apps/publish/store-policies" target="_blank" rel="noreferrer">Store の規約</a></p>
</main>
<script>
for (const button of document.querySelectorAll("button.copy")) {
  button.addEventListener("click", async () => {
    const box = button.closest(".field").querySelector("textarea");
    box.select();
    let copied = false;
    try { await navigator.clipboard.writeText(box.value); copied = true; } catch (e) {
      try { copied = document.execCommand("copy"); } catch (e2) { copied = false; }
    }
    button.textContent = copied ? "コピーした" : "Ctrl+C で";
    setTimeout(() => (button.textContent = "コピー"), 2500);
  });
}
for (const box of document.querySelectorAll("input[data-step]")) {
  const key = "store-guide-" + box.dataset.step;
  try { box.checked = localStorage.getItem(key) === "1"; } catch (e) { /* not kept */ }
  box.addEventListener("change", () => {
    try { localStorage.setItem(key, box.checked ? "1" : "0"); } catch (e) { /* not kept */ }
  });
}
</script>
</body></html>
"""


def main() -> None:
    page = guide()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(page, encoding="utf-8", newline="\n")
    print(OUT)


if __name__ == "__main__":
    main()
