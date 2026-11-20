// The page's texts, part "reader" (task 11-5): the article reader's own words
// (reader.html, reader/reader.ts), merged into the tables by ../ja.ts and
// ../en.ts. The reader gets the main page's language in its address
// (reader.html?lang=en), since it has no storage of the main page's.
//
// The articles themselves are content, not these words: ui/public/articles/
// (<id>.md, and their titles in articles/index.json). Their English comes with
// the articles (task 7-3), not here.
//
// English: with the glossary (docs/i18n-glossary.md).

export const ja = {
  // The page's title, alone (the list, an article not found) and after an article's title.
  // The app's window title says the same (app/logic/StartupTexts.cpp, ArticleWindowTitle).
  "reader.title": "記事",
  "reader.articleTitle": "{title} — 記事",
  // The button at the top, back to the list.
  "reader.toList": "記事の一覧",
  // The list of articles.
  "reader.list.heading": "記事の一覧",
  "reader.list.empty": "記事がまだありません。",
  // An address naming an article that is not there.
  "reader.missing": "この記事は見つかりません。",
} as const;

export const en: Record<keyof typeof ja, string> = {
  "reader.title": "Articles",
  "reader.articleTitle": "{title} — Articles",
  "reader.toList": "All articles",
  "reader.list.heading": "All articles",
  "reader.list.empty": "No articles yet.",
  "reader.missing": "This article was not found.",
};
