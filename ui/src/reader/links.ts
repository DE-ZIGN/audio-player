// Task 7-1: links in an article.
//
// The page follows a link to another article (or a section) itself. Anything
// else it asks the window to open, and the app decides there (app/logic/
// ArticlePolicy.h): a listed command "app:<name>" goes to the main screen, a
// web address to the default browser, the rest is refused. Nothing here is
// trusted by the app: this file only saves a round trip for article links.

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

// How a link is marked on the page. Looks only.
export function linkKind(href: string): "article" | "command" | "web" | "other" {
  if (linkTarget(href).kind === "article") return "article";
  if (/^app:/.test(href)) return "command";
  if (/^https?:\/\//.test(href)) return "web";
  return "other";
}
