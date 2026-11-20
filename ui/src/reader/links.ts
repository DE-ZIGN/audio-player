// Task 7-1: links in an article.
//
// The page follows a link to another article (or a section) itself. Anything
// else it asks the window to open, and the app decides there (app/logic/
// ArticlePolicy.h): a listed command "app:<name>" goes to the main screen, a
// web address to the default browser, the rest is refused. Nothing here is
// trusted by the app: this file only saves a round trip for article links.

import { LANGS, type Lang } from "../i18n";

export type LinkTarget =
  | { kind: "article"; id: string; section: string } // id "": this article
  | { kind: "native"; url: string };

const ID = /^[a-z0-9][a-z0-9-]{0,63}$/;
const SECTION = /^[A-Za-z0-9-]{1,64}$/;
const ARTICLE_LINK = /^(?:\.\/)?([a-z0-9][a-z0-9-]{0,63})(?:\.md)?(?:#([A-Za-z0-9-]{1,64}))?$/;
const SECTION_LINK = /^#([A-Za-z0-9-]{1,64})$/;

// An article's fixed id (as isArticleId in ArticlePolicy.h).
export function isArticleId(id: string): boolean {
  return ID.test(id);
}

export function isSection(section: string): boolean {
  return SECTION.test(section);
}

export function linkTarget(href: string): LinkTarget {
  const section = SECTION_LINK.exec(href);
  if (section) return { kind: "article", id: "", section: section[1] };
  const article = ARTICLE_LINK.exec(href);
  if (article) return { kind: "article", id: article[1], section: article[2] ?? "" };
  return { kind: "native", url: href };
}

// The reader's address (task 11-5): reader.html?lang=<ja|en>&id=<article>#<section>,
// as articleReaderUrl in ArticlePolicy.cpp builds it. The language is the main
// page's, carried in the address because the reader has no storage of the main
// page's; the reader keeps it when it moves inside itself. What is not an id,
// a section or one of the languages is left out.
export function readerAddress(id: string, section: string, lang?: Lang): string {
  const query = [
    ...(lang !== undefined && (LANGS as readonly string[]).includes(lang) ? [`lang=${lang}`] : []),
    ...(isArticleId(id) ? [`id=${id}`] : []),
  ];
  return `reader.html${query.length ? `?${query.join("&")}` : ""}${isArticleId(id) && isSection(section) ? `#${section}` : ""}`;
}

// The language the address names ("?lang=en"), if it is one of the page's; else
// none, and the reader uses the default rule (i18n.ts, defaultLang).
export function readerLang(search: string): Lang | undefined {
  const lang = new URLSearchParams(search).get("lang");
  return lang !== null && (LANGS as readonly string[]).includes(lang) ? (lang as Lang) : undefined;
}

// How a link is marked on the page. Looks only.
export function linkKind(href: string): "article" | "command" | "web" | "other" {
  if (linkTarget(href).kind === "article") return "article";
  if (/^app:/.test(href)) return "command";
  if (/^https?:\/\//.test(href)) return "web";
  return "other";
}
