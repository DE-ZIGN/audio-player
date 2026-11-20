// The page's languages (task 11-1, M11; CLAUDE.md: 多言語化): Japanese and
// English. Every text the page shows is in the tables (i18n/ja.ts,
// i18n/en.ts) and read with t(key, values).
//
// Which language: the one the user chose in the settings (kept in the web
// view's storage), else the Windows display language (navigator.languages in
// WebView2): Japanese for Japanese, English for any other. A change applies at
// once: the page's data-i18n texts are filled again, and whoever listens
// (onLangChange) draws its own texts again.

import { en } from "./i18n/en";
import { ja, type MessageKey, type Messages } from "./i18n/ja";

export type Lang = "ja" | "en";
export const LANGS: readonly Lang[] = ["ja", "en"];
export const LANG_KEY = "language"; // the language chosen (localStorage)

const TABLES: Record<Lang, Messages> = { ja, en };

let lang: Lang = "ja";
const listeners = new Set<(l: Lang) => void>();

export type Values = Record<string, string | number>;

// "{name}" in `text` replaced by values.name; a name not given stays as it is
// (seen on the screen, rather than silently empty).
export function format(text: string, values: Values = {}): string {
  return text.replace(/\{([A-Za-z0-9_]+)\}/g, (all, name: string) =>
    Object.prototype.hasOwnProperty.call(values, name) ? String(values[name]) : all,
  );
}

// The names of the values a text takes, sorted, once each.
export function placeholders(text: string): string[] {
  return [...new Set([...text.matchAll(/\{([A-Za-z0-9_]+)\}/g)].map((m) => m[1]))].sort();
}

export function t(key: MessageKey, values?: Values): string {
  return format(TABLES[lang][key], values);
}

export function currentLang(): Lang {
  return lang;
}

// Japanese for a Japanese Windows, English for any other.
export function defaultLang(languages: readonly string[]): Lang {
  return languages.length > 0 && /^ja\b/i.test(languages[0]) ? "ja" : "en";
}

export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

function isLang(v: string | null): v is Lang {
  return v !== null && (LANGS as readonly string[]).includes(v);
}

export function loadLang(storage: StorageLike | null, languages: readonly string[]): Lang {
  try {
    const v = storage?.getItem(LANG_KEY) ?? null;
    if (isLang(v)) return v;
  } catch {
    // no storage: the default
  }
  return defaultLang(languages);
}

export function saveLang(storage: StorageLike | null, l: Lang) {
  try {
    storage?.setItem(LANG_KEY, l);
  } catch {
    // not kept: the language lasts until the window closes
  }
}

function applyToPage() {
  if (typeof document === "undefined") return; // (the tests)
  document.documentElement.lang = lang;
  translateTree(document);
}

export function setLang(l: Lang) {
  if (l === lang) return;
  lang = l;
  applyToPage();
  for (const f of listeners) f(l);
}

export function onLangChange(f: (l: Lang) => void): () => void {
  listeners.add(f);
  return () => listeners.delete(f);
}

// What translateTree needs of the page (an element or the document).
interface TextNodeLike {
  textContent: string | null;
  getAttribute(name: string): string | null;
  setAttribute(name: string, value: string): void;
}
interface TreeLike {
  querySelectorAll(selector: string): ArrayLike<TextNodeLike> | Iterable<TextNodeLike>;
}

// The attributes a key can fill, besides the text: data-i18n-<attribute>.
const ATTRIBUTES = ["title", "aria-label", "placeholder"] as const;

function known(key: string | null): key is MessageKey {
  return key !== null && Object.prototype.hasOwnProperty.call(ja, key);
}

// Fills the elements marked in the page: data-i18n="key" sets the text (the
// element must hold text only), data-i18n-title="key" etc. set attributes.
export function translateTree(root: TreeLike) {
  for (const e of Array.from(root.querySelectorAll("[data-i18n]") as Iterable<TextNodeLike>)) {
    const key = e.getAttribute("data-i18n");
    if (known(key)) e.textContent = t(key);
  }
  for (const a of ATTRIBUTES) {
    for (const e of Array.from(root.querySelectorAll(`[data-i18n-${a}]`) as Iterable<TextNodeLike>)) {
      const key = e.getAttribute(`data-i18n-${a}`);
      if (known(key)) e.setAttribute(a, t(key));
    }
  }
}

// At start: the language chosen or the default, filled in before anything is
// drawn. `forced` is for the app's self test, which checks Japanese texts
// whatever the machine's language (the CI runner is an English Windows); it is
// not saved.
export function startLang(storage: StorageLike | null, languages: readonly string[], forced?: Lang) {
  lang = forced ?? loadLang(storage, languages);
  applyToPage();
}

export type { MessageKey };
