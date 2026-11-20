import { describe, expect, it } from "vitest";
import {
  defaultLang,
  format,
  LANG_KEY,
  LANGS,
  loadLang,
  placeholders,
  saveLang,
  setLang,
  currentLang,
  onLangChange,
  t,
  translateTree,
  type Lang,
} from "./i18n";
import { BASE_JA, ja, PARTS } from "./i18n/ja";
import { en } from "./i18n/en";
import html from "../index.html?raw";
import { SETTINGS_TABS } from "./settingsTabs";

// Task 11-1 (M11, 2026-11-20): every text the page shows comes from the
// tables, Japanese and English (CLAUDE.md: 多言語化).

const TABLES: Record<Lang, Record<string, string>> = { ja, en };

// Every source file of the page (tests aside), as text.
const sources = import.meta.glob(["./**/*.ts", "!./**/*.test.ts", "!./i18n/**/*.ts"], {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>;

// The keys the page names: data-i18n="key" (and data-i18n-title etc.) in the
// page, t("key", ...) in the scripts. (TypeScript checks the scripts' keys too.)
function keysNamed(): Set<string> {
  const used = new Set<string>();
  const add = (text: string, re: RegExp) => {
    for (const m of text.matchAll(re)) used.add(m[1]);
  };
  add(html, /data-i18n(?:-[a-z-]+)?="([^"]+)"/g);
  for (const text of Object.values(sources)) add(text, /\bt\(\s*"([^"]+)"/g);
  return used;
}

// A key is in use when the page or a script has it written out as "key" (a
// table of keys, as the settings tabs have, counts).
function inUse(key: string): boolean {
  const quoted = `"${key}"`;
  return html.includes(quoted) || Object.values(sources).some((text) => text.includes(quoted));
}

class MemoryStorage {
  values = new Map<string, string>();
  getItem(k: string) {
    return this.values.get(k) ?? null;
  }
  setItem(k: string, v: string) {
    this.values.set(k, v);
  }
}

describe("the tables", () => {
  it("the parts never share a key, and each part's keys start with its name", () => {
    const sizes = Object.keys(BASE_JA).length + Object.values(PARTS).reduce((n, p) => n + Object.keys(p.ja).length, 0);
    expect(Object.keys(ja).length).toBe(sizes);
    for (const [name, p] of Object.entries(PARTS))
      for (const k of Object.keys(p.ja)) expect(k.startsWith(`${name}.`), k).toBe(true);
  });
  it("both languages have the same keys, none empty", () => {
    expect(Object.keys(en).sort()).toEqual(Object.keys(ja).sort());
    for (const lang of LANGS)
      for (const [k, v] of Object.entries(TABLES[lang])) expect(v.trim().length, `${lang} ${k}`).toBeGreaterThan(0);
  });
  it("a text's values have the same names in both languages", () => {
    for (const k of Object.keys(ja)) expect(placeholders(en[k as keyof typeof en]), k).toEqual(placeholders(ja[k as keyof typeof ja]));
  });
  it("every key the page uses is in the tables, and every key in the tables is used", () => {
    const known = new Set(Object.keys(ja));
    expect([...keysNamed()].filter((k) => !known.has(k))).toEqual([]);
    // The app's messages ("native.<code>", task 11-3) are used through their
    // codes: nativeText.test.ts checks them against app/logic/NativeMessages.h.
    expect([...known].filter((k) => !k.startsWith("native.") && !inUse(k))).toEqual([]);
  });
  it("the English never says the sound gets better (CLAUDE.md chapter 1)", () => {
    const claims = /\b(improves?|improved|improving|better sound|sounds? better|enhances?|enhanced|superior|purer|cleaner sound)\b/i;
    for (const [k, v] of Object.entries(en)) expect(v, k).not.toMatch(claims);
  });
  it("the language setting's own words read the same in either language", () => {
    for (const k of ["settings.look.language", "lang.ja", "lang.en"] as const) expect(en[k]).toBe(ja[k]);
  });
});

describe("t and format", () => {
  it("put the values in", () => {
    expect(format("{n} 曲・{m} 枚", { n: 3, m: "2" })).toBe("3 曲・2 枚");
    expect(format("no values")).toBe("no values");
    expect(format("{a}{a}", { a: 1 })).toBe("11");
  });
  it("leave a value they were not given as it is (seen, not hidden)", () => {
    expect(format("{n} 曲", {})).toBe("{n} 曲");
  });
  it("read the language in use", () => {
    setLang("ja");
    expect(t("settings.tab.output")).toBe(ja["settings.tab.output"]);
    setLang("en");
    expect(t("settings.tab.output")).toBe(en["settings.tab.output"]);
    setLang("ja");
  });
  it("placeholders lists a text's value names, once each", () => {
    expect(placeholders("{b} と {a} と {b}")).toEqual(["a", "b"]);
    expect(placeholders("none")).toEqual([]);
  });
});

describe("which language", () => {
  it("by default the Windows display language: Japanese for Japanese, English for any other", () => {
    expect(defaultLang(["ja-JP"])).toBe("ja");
    expect(defaultLang(["ja"])).toBe("ja");
    expect(defaultLang(["en-US", "ja"])).toBe("en");
    expect(defaultLang(["de-DE"])).toBe("en");
    expect(defaultLang(["zh-CN"])).toBe("en");
    expect(defaultLang([])).toBe("en");
  });
  it("the one the user chose comes first, and is kept", () => {
    const s = new MemoryStorage();
    expect(loadLang(s, ["ja-JP"])).toBe("ja");
    saveLang(s, "en");
    expect(s.getItem(LANG_KEY)).toBe("en");
    expect(loadLang(s, ["ja-JP"])).toBe("en");
    s.setItem(LANG_KEY, "fr"); // not one of ours: the default
    expect(loadLang(s, ["ja-JP"])).toBe("ja");
  });
  it("no storage: the default, and choosing does not fail", () => {
    const broken = {
      getItem: () => {
        throw new Error("blocked");
      },
      setItem: () => {
        throw new Error("blocked");
      },
    };
    expect(loadLang(broken, ["en-GB"])).toBe("en");
    expect(() => saveLang(broken, "ja")).not.toThrow();
  });
  it("a change is told to whoever listens, and sets the page's lang", () => {
    const seen: Lang[] = [];
    const stop = onLangChange((l) => seen.push(l));
    setLang("en");
    setLang("en"); // no change: not told again
    setLang("ja");
    stop();
    setLang("en");
    setLang("ja");
    expect(seen).toEqual(["en", "ja"]);
    expect(currentLang()).toBe("ja");
  });
});

describe("the page", () => {
  it("translateTree fills data-i18n texts and attributes, and leaves the rest", () => {
    // A stand-in for the page (the tests run without a browser).
    const el = (attrs: Record<string, string>, text = "") => {
      const a = new Map(Object.entries(attrs));
      return {
        textContent: text,
        getAttribute: (n: string) => a.get(n) ?? null,
        setAttribute: (n: string, v: string) => void a.set(n, v),
        attrs: a,
      };
    };
    const span = el({ "data-i18n": "settings.tab.output" }, "x");
    const button = el({ "data-i18n-title": "settings.look.languageNote", "data-i18n-aria-label": "settings.look.language" }, "keep");
    const all = [span, button];
    const root = {
      querySelectorAll: (sel: string) => {
        const m = /^\[([a-z0-9-]+)\]$/.exec(sel);
        return m ? all.filter((e) => e.getAttribute(m[1]) !== null) : [];
      },
    };
    setLang("en");
    translateTree(root);
    expect(span.textContent).toBe(en["settings.tab.output"]);
    expect(button.attrs.get("title")).toBe(en["settings.look.languageNote"]);
    expect(button.attrs.get("aria-label")).toBe(en["settings.look.language"]);
    expect(button.textContent).toBe("keep");
    setLang("ja");
    translateTree(root);
    expect(span.textContent).toBe(ja["settings.tab.output"]);
  });
  it("the page starts in Japanese (lang=ja) and its Japanese texts are the tables' own", () => {
    expect(html).toMatch(/<html lang="ja"/);
    for (const m of html.matchAll(/<(\w+)[^>]*data-i18n="([^"]+)"[^>]*>([^<]*)</g)) {
      expect(m[3], m[2]).toBe(ja[m[2] as keyof typeof ja]);
    }
  });
  it("the settings screen offers the two languages, under a heading that reads in both", () => {
    const look = html.slice(html.indexOf('data-group="look"'), html.indexOf('data-group="articles"'));
    expect(look).toMatch(/data-i18n="settings\.look\.language"/);
    for (const l of LANGS) expect(look).toMatch(new RegExp(`<input type="radio" name="language" value="${l}"`));
  });
  it("the settings tabs are named from the tables", () => {
    for (const tab of SETTINGS_TABS) expect(ja[tab.labelKey].length).toBeGreaterThan(0);
  });
});
