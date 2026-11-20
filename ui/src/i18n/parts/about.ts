// The page's texts, part "about" (task 8-2): the settings' 情報 / About tab
// (aboutView.ts), merged into the tables by ../ja.ts and ../en.ts. The
// components' names, versions, copyright lines and license texts are their
// own (English, from third_party/components.json and their files): not here,
// not translated.
//
// English: with the glossary (docs/i18n-glossary.md).

export const ja = {
  // The app itself.
  "about.heading": "このアプリ",
  "about.app.version": "バージョン {version}",
  "about.app.free":
    "このアプリは自由ソフトウェアです。GNU Affero General Public License 第 3 版（AGPLv3）の条件のもとで、使う・調べる・変える・配ることができます。保証はありません（くわしくはライセンス本文）。",
  "about.label.license": "ライセンス",
  "about.label.source": "ソースコード",
  // The heading over the trademark notices.
  "about.trademarks": "商標",
  // As THIRD_PARTY_NOTICES.md writes it (about.test.ts checks it).
  "about.trademark.asio": "ASIO は Steinberg Media Technologies GmbH の商標です。本製品は ASIO のロゴを使っていません。",

  // The components.
  "about.components.heading": "使っている部品",
  "about.components.note":
    "アプリに組み込んで配っている部品です。それぞれの著作権表示とライセンスの本文を載せています。ビルドとテストにだけ使うもの（配布物に含まないもの）は載せていません。",
  "about.label.version": "バージョン",
  "about.label.url": "入手先",
  "about.label.copyright": "著作権表示",
  // After a part bundled in another ("zlib" in "JUCE").
  "about.bundledIn": "{name} に同梱",
  // The button that opens one license text, with the file's own name.
  "about.licenseText": "ライセンス本文（{name}）",
  "about.textLoading": "読み込み中…",
  "about.textError": "ライセンス本文を読めませんでした（{detail}）",
  // The browser's mock (npm run dev): the build makes the list, so there is none.
  "about.noData":
    "部品の一覧は、アプリのビルドが作ります。開発用のサーバー（ブラウザーの模擬）には一覧がないので、ここには出ません。",
  "about.loadError": "部品の一覧を読めませんでした（{detail}）",

  // What a component's license asks to be said (third_party/components.json, "note").
  "about.note.juce": "AGPLv3 と商用ライセンスのデュアルライセンスのうち、AGPLv3 で使っています。",
  "about.note.asio": "Steinberg ASIO License と GPLv3 のデュアルライセンスのうち、GPLv3 で使っています。",
  "about.note.alac":
    "変更あり：公開版のデコーダーには入力の検査がないため、値の検査を足しました。変更点の一覧は、ソースの third_party/alac/VENDORED.md にあります。",
  "about.note.mpg123":
    "mpg123.dll として、アプリの実行ファイルの隣に置いています。同じ API を持つ別のビルドに差し替えられます（LGPL 2.1）。",
  "about.note.taglib":
    "LGPL 2.1 と MPL 1.1 のうち、LGPL 2.1 で使っています。静的にリンクしています。このアプリのソースは公開しているので、TagLib を差し替えてリンクし直せます。",
  "about.note.mair":
    "変更あり：2 本のモノラルを 1 本のステレオにまとめ、直接音の 1 ms 前より前を切りました（値はそのまま）。空間の HALL の「内蔵（実測）」に使っています。",
} as const;

export const en: Record<keyof typeof ja, string> = {
  "about.heading": "This app",
  "about.app.version": "Version {version}",
  "about.app.free":
    "This app is free software: you may use, study, change and share it under the terms of the GNU Affero General Public License, version 3 (AGPLv3). It comes with no warranty (see the license text).",
  "about.label.license": "License",
  "about.label.source": "Source code",
  "about.trademarks": "Trademarks",
  "about.trademark.asio": "ASIO is a trademark of Steinberg Media Technologies GmbH. This product does not use the ASIO logo.",

  "about.components.heading": "Components",
  "about.components.note":
    "The components built into the app and distributed with it, each with its copyright notice and the full text of its license. Those used only to build and test the app (not distributed) are not listed.",
  "about.label.version": "Version",
  "about.label.url": "Website",
  "about.label.copyright": "Copyright",
  "about.bundledIn": "Bundled in {name}",
  "about.licenseText": "License text ({name})",
  "about.textLoading": "Loading…",
  "about.textError": "Could not read the license text ({detail})",
  "about.noData":
    "The list of components is made by the app's build. The development server (the browser's mock) does not have it, so it is not shown here.",
  "about.loadError": "Could not read the list of components ({detail})",

  "about.note.juce": "Dual-licensed under the AGPLv3 and a commercial license; used here under the AGPLv3.",
  "about.note.asio": "Dual-licensed under the Steinberg ASIO License and the GPLv3; used here under the GPLv3.",
  "about.note.alac":
    "Modified: checks of the input values were added, as the published decoder has none. The changes are listed in third_party/alac/VENDORED.md in the source.",
  "about.note.mpg123":
    "Shipped as mpg123.dll next to the app's executable. You may replace it with another build that has the same API (LGPL 2.1).",
  "about.note.taglib":
    "Offered under the LGPL 2.1 or the MPL 1.1; used here under the LGPL 2.1. It is linked statically; the app's source is public, so TagLib can be replaced and the app linked again.",
  "about.note.mair":
    "Modified: the two mono files were joined into one stereo file, and everything before 1 ms ahead of the direct sound was cut (sample values unchanged). Used as HALL's built-in (measured) IR in Space.",
};
