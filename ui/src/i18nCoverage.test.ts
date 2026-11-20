import { describe, expect, it } from "vitest";
import html from "../index.html?raw";

// Task 11-2 (M11): no Japanese is left in the page or its scripts outside the
// tables (i18n/), so the English table (11-4) covers everything shown. Comments
// may be in any language.

const JAPANESE = /[　-ヿ㐀-鿿！-｠]/;

// What is left on purpose, and why.
const OUT_OF_SCOPE: [RegExp, string][] = [
  [/\/i18n\//, "the tables themselves"],
  [/\/selftest\.ts$/, "the app's self test: it runs in Japanese and checks the Japanese texts"],
  [/\/dev\//, "development tools, not in the app"],
  [/\/mock[A-Z][^/]*\.ts$/, "the mock engine's data (track names, devices); its messages become codes in 11-3"],
  [/\/backend\.ts$/, "the mock engine: its messages stand in for the app's own, which become codes in 11-3"],
  [/\/reader\//, "the article reader: task 11-5"],
];

// Japanese that is data, not words of the screen, in files otherwise covered.
const DATA: Record<string, RegExp[]> = {
  // What people type, read the same in full-width forms: the full-width
  // characters and space (！ to ～, the ideographic space) turned into ASCII.
  "./tubeAuto.ts": [/\[！-～\]/g, /\[　\\t \]/g],
  // A number typed with a full-width comma (，) as the decimal point.
  "./eqEdit.ts": [/\[，,\]/g],
};

const sources = import.meta.glob(["./**/*.ts", "!./**/*.test.ts"], {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>;

// The code of a script without its comments (strings kept: a "//" inside one
// is rare enough here to be found by the failure it would cause).
function withoutComments(text: string): string {
  return text
    .replace(/\r\n/g, "\n") // (a working copy may have CRLF: "$" then misses the comment's end)
    .replace(/\/\*[\s\S]*?\*\//g, "")
    .split("\n")
    .map((line) => line.replace(/(^|[^:"'`])\/\/.*$/, "$1"))
    .join("\n");
}

describe("no Japanese outside the tables", () => {
  it("in the scripts (comments aside)", () => {
    const left: string[] = [];
    for (const [file, text] of Object.entries(sources)) {
      if (OUT_OF_SCOPE.some(([re]) => re.test(file))) continue;
      let code = withoutComments(text);
      for (const re of DATA[file] ?? []) code = code.replace(re, '""');
      code.split("\n").forEach((line, i) => {
        if (JAPANESE.test(line)) left.push(`${file}:${i + 1}: ${line.trim().slice(0, 90)}`);
      });
    }
    expect(left).toEqual([]);
  });

  it("in the page: each Japanese text is marked with its key, and so is each Japanese attribute", () => {
    const page = html.replace(/<!--[\s\S]*?-->/g, "");
    const left: string[] = [];
    // A text run: what follows an opening tag up to the next tag.
    for (const m of page.matchAll(/<([a-z][a-z0-9]*)((?:\s[^>]*)?)>([^<]*)/gi)) {
      const [, , attrs, text] = m;
      if (!JAPANESE.test(text)) continue;
      if (!/\sdata-i18n="[^"]+"/.test(attrs)) left.push(`text: ${text.trim().slice(0, 60)}`);
    }
    // A text run after a closing tag (<b>x</b> 日本語): never marked, so never allowed.
    for (const m of page.matchAll(/<\/[a-z0-9]+>([^<]*)/gi))
      if (JAPANESE.test(m[1])) left.push(`text after a closing tag: ${m[1].trim().slice(0, 60)}`);
    for (const m of page.matchAll(/<[a-z][a-z0-9]*\s([^>]*)>/gi)) {
      for (const a of m[1].matchAll(/\s?(title|aria-label|placeholder|alt)="([^"]*)"/g)) {
        if (JAPANESE.test(a[2]) && !new RegExp(`data-i18n-${a[1]}="[^"]+"`).test(m[1])) left.push(`${a[1]}: ${a[2].slice(0, 60)}`);
      }
    }
    expect(left).toEqual([]);
  });

  it("what is left out is named, with its reason", () => {
    for (const [re, why] of OUT_OF_SCOPE) expect(why.length, String(re)).toBeGreaterThan(10);
  });
});
