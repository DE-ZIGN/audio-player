import "./reader.css";
import { startLang, t } from "../i18n";
import { isArticleId, isSection, linkKind, linkTarget, readerAddress, readerLang } from "./links";
import { parseArticle, type Block, type Inline } from "./markdown";

// The article reader's page (task 7-1). It runs in a WebView of its own with
// no native functions: it can only show articles and ask the window to open
// an address, which the app allows or refuses (app/logic/ArticlePolicy.h).
//
// Articles are text. Elements are made one by one from the parsed blocks;
// nothing from an article is ever set as HTML.
//
// Its own words are in the page's tables (i18n/parts/reader.ts), in the main
// page's language, which comes in the address (?lang=en, task 11-5): the reader
// has its own storage, so it cannot read the main page's choice. Without one,
// the main page's default rule (Japanese for a Japanese Windows, else English).
// The articles stay as they are: an English article would be its own file next
// to the Japanese one in ui/public/articles/ (task 7-3), which this page would
// then pick by `lang` (not built yet).

interface Entry {
  id: string;
  title: string;
}

// The language first, before anything is drawn (no storage: the main page's is not ours).
const lang = readerLang(location.search);
startLang(null, navigator.languages ?? [navigator.language], lang);

const article = document.getElementById("article") as HTMLElement;
const toList = document.getElementById("to-list") as HTMLButtonElement;

function el<K extends keyof HTMLElementTagNameMap>(tag: K, className = "", text = ""): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text) e.textContent = text;
  return e;
}

function link(text: string, href: string): HTMLAnchorElement {
  const a = el("a", `link-${linkKind(href)}`, text);
  a.setAttribute("href", href); // as written: the click handler below decides, then the app
  a.rel = "noreferrer noopener";
  return a;
}

function inlinesInto(parent: HTMLElement, inlines: Inline[]) {
  for (const i of inlines) parent.append(i.kind === "link" ? link(i.text, i.href) : document.createTextNode(i.text));
}

function render(blocks: Block[]): DocumentFragment {
  const out = document.createDocumentFragment();
  for (const b of blocks) {
    if (b.kind === "heading") {
      const h = el(`h${b.level}`);
      if (b.id) h.id = b.id;
      inlinesInto(h, b.inlines);
      out.append(h);
    } else if (b.kind === "paragraph") {
      const p = el("p");
      inlinesInto(p, b.inlines);
      out.append(p);
    } else {
      const ul = el("ul");
      for (const item of b.items) {
        const li = el("li");
        inlinesInto(li, item);
        ul.append(li);
      }
      out.append(ul);
    }
  }
  return out;
}

// Only the reader's own files (the page's policy allows nothing else).
async function fetchText(path: string): Promise<string | null> {
  try {
    const r = await fetch(path, { cache: "no-store" });
    return r.ok ? await r.text() : null;
  } catch {
    return null;
  }
}

async function loadIndex(): Promise<Entry[]> {
  try {
    const list: unknown = JSON.parse((await fetchText("articles/index.json")) ?? "[]");
    if (!Array.isArray(list)) return [];
    return list.flatMap((e: { id?: unknown; title?: unknown }) =>
      typeof e?.id === "string" && isArticleId(e.id) && typeof e.title === "string" && e.title ? [{ id: e.id, title: e.title }] : [],
    );
  } catch {
    return [];
  }
}

function current(): { id: string; section: string } {
  const id = new URLSearchParams(location.search).get("id") ?? "";
  const section = location.hash.slice(1);
  return { id: isArticleId(id) ? id : "", section: isSection(section) ? section : "" };
}

async function showList() {
  const entries = await loadIndex();
  const nodes: Node[] = [el("h1", "", t("reader.list.heading"))];
  if (entries.length === 0) nodes.push(el("p", "missing", t("reader.list.empty")));
  const ul = el("ul", "article-list");
  for (const e of entries) {
    const li = el("li");
    li.append(link(e.title, `${e.id}.md`));
    ul.append(li);
  }
  nodes.push(ul);
  article.replaceChildren(...nodes);
  document.title = t("reader.title");
  toList.hidden = true;
  scrollTo(0, 0);
}

async function showArticle(id: string, section: string) {
  const text = await fetchText(`articles/${id}.md`);
  toList.hidden = false;
  if (text === null) {
    article.replaceChildren(el("p", "missing", t("reader.missing")));
    document.title = t("reader.title");
    return;
  }
  const blocks = parseArticle(text);
  article.replaceChildren(render(blocks));
  const title = blocks.find((b) => b.kind === "heading" && b.level === 1);
  document.title =
    title && title.kind === "heading"
      ? t("reader.articleTitle", { title: title.inlines.map((i) => i.text).join("") })
      : t("reader.title");
  const target = section ? document.getElementById(section) : null;
  if (target && article.contains(target)) target.scrollIntoView();
  else scrollTo(0, 0);
}

let shown = "\0"; // the article on the page ("" is the list)

async function showCurrent() {
  const { id, section } = current();
  if (id === shown) {
    // Same article: only the section.
    const target = section ? document.getElementById(section) : null;
    if (target && article.contains(target)) target.scrollIntoView();
    return;
  }
  shown = id;
  if (id) await showArticle(id, section);
  else await showList();
}

function go(id: string, section: string) {
  const here = current();
  const to = id || here.id;
  history.pushState(null, "", readerAddress(to, section, lang)); // the language kept
  void showCurrent();
}

document.addEventListener("click", (e) => {
  const a = (e.target as Element | null)?.closest?.("a");
  if (!a) return;
  e.preventDefault();
  const target = linkTarget(a.getAttribute("href") ?? "");
  if (target.kind === "article") go(target.id, target.section);
  // A command, a web link or anything else: the window asks the app, which
  // sends it to the main screen or the default browser, or refuses it.
  else if (target.url) location.assign(target.url);
});
// A middle click would ask for a new window: the same question to the app, one way only.
document.addEventListener("auxclick", (e) => {
  if ((e.target as Element | null)?.closest?.("a")) e.preventDefault();
});
toList.addEventListener("click", () => {
  history.pushState(null, "", readerAddress("", "", lang)); // the list, in the same language
  void showCurrent();
});
addEventListener("popstate", () => void showCurrent());

void showCurrent();
