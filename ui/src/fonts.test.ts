import { describe, expect, it } from "vitest";
import css from "./style.css?raw";

// Task 6-10 (2026-11-15, the user): one typeface in every view - Segoe UI
// with Yu Gothic UI - and no Mincho anywhere. Only the words engraved on the
// amp's metal panel keep the concept images' serif capitals. The settings
// screen: white text, 16 px, the explanations 14 px, at their real size in
// the zoomed standard and amp views too.

interface Rule {
  selector: string;
  body: string;
}

function rules(text: string): Rule[] {
  const out: Rule[] = [];
  const plain = text.replace(/\/\*[\s\S]*?\*\//g, "");
  const re = /([^{}]+)\{([^{}]*)\}/g;
  let m: RegExpExecArray | null;
  while ((m = re.exec(plain))) out.push({ selector: m[1].trim(), body: m[2] });
  return out;
}

const all = rules(css);
// The amp's engraved panel words (the only serif left).
const PANEL = /^(\.amp-word|\.amp-ch|\.amp-badge|\[data-look="real"\] \.amp-volume-slot \.vol-label|\.amp-key)\b/;

describe("one typeface (task 6-10)", () => {
  it("uses no Mincho anywhere", () => {
    expect(css.replace(/\/\*[\s\S]*?\*\//g, "")).not.toMatch(/Mincho/i); // (comments may name it)
  });
  it("keeps a serif only on the amp's panel", () => {
    const serif = all.filter((r) => /font-family:[^;]*serif/.test(r.body.replace(/sans-serif/g, "")));
    expect(serif.map((r) => r.selector).filter((s) => !PANEL.test(s))).toEqual([]);
    expect(serif.length).toBeGreaterThan(0);
  });
  it("does not give the standard view other fonts for titles and lists", () => {
    const overrides = all.filter((r) => /--font-(display|list):/.test(r.body) && !/:\s*inherit/.test(r.body));
    expect(overrides.map((r) => r.selector)).toEqual([]);
  });
});

describe("the settings screen's type (task 6-10)", () => {
  const body = (sel: string) => all.find((r) => r.selector === sel)?.body ?? "";
  it("is white, 16 px, with the explanations at 14 px", () => {
    expect(body("#settings")).toMatch(/color:\s*#fff\b/);
    expect(body("#settings")).toMatch(/font-size:\s*calc\(16 \* var\(--spx\)\)/);
    expect(body("#settings :where(*:not(svg, svg *))")).toMatch(/font-size:\s*inherit/);
    expect(body("#settings :where(*:not(svg, svg *))")).toMatch(/color:\s*inherit/);
    expect(all.some((r) => r.selector.startsWith("#settings :is(.note") && /calc\(14 \* var\(--spx\)\)/.test(r.body))).toBe(true);
  });
  it("is at its real size in the zoomed views", () => {
    expect(body("#settings")).toMatch(/--spx:\s*1px/);
    expect(body('[data-look="real"] #settings')).toMatch(/--spx:\s*calc\(1px \/ var\(--ui-zoom, 1\)\)/);
  });
  it("keeps the buttons on the accent dark, and warnings in their colour", () => {
    expect(all.some((r) => /^#settings :is\(button\.primary, button\.active\)/.test(r.selector) && /var\(--accent-text\)/.test(r.body))).toBe(true);
    expect(all.some((r) => /^#settings \.notice/.test(r.selector) && /var\(--warn\)/.test(r.body))).toBe(true);
  });
});

describe("the settings screen's look (task 6-10, 2026-11-15)", () => {
  const body = (sel: string) => all.find((r) => r.selector === sel)?.body ?? "";
  it("does not dim the 音作りモード sections in 原音モード (the text stays white); the heading says so", () => {
    expect(all.filter((r) => /\.original-mode .*-controls/.test(r.selector) && /opacity/.test(r.body))).toEqual([]);
    expect(body(".settings.original-mode .for-mode::after")).toMatch(/content:\s*"/);
  });
  it("has headings that read as headings, room before each, and looser lines", () => {
    expect(body("#settings h2")).toMatch(/font-size:\s*calc\(20 \* var\(--spx\)\)/);
    expect(body("#settings h2")).toMatch(/font-weight:\s*700/);
    expect(body("#settings h2")).toMatch(/margin:\s*40px 0 10px/);
    expect(body("#settings")).toMatch(/line-height:\s*1\.75/);
  });
  it("lines the sliders up: one width for every label", () => {
    expect(body("#settings .slider-row")).toMatch(/grid-template-columns:\s*7em /);
    expect(body("#settings .slider-row")).toMatch(/display:\s*grid/);
  });
  it("shows that clickable words can be pressed (buttons)", () => {
    const r = all.find((x) => x.selector.startsWith("#settings :is(button.link, details > summary)"));
    expect(r?.body).toMatch(/border:\s*1px solid/);
    expect(r?.body).toMatch(/cursor:\s*pointer/);
  });
});

describe("the side panels in the zoomed views (task 6-10, 2026-11-15)", () => {
  it("are at their real size, like the library view's", () => {
    const r = all.find((x) => x.selector === '[data-look="real"] :is(.sidebar, .queue-panel) > *');
    expect(r?.body).toMatch(/zoom:\s*calc\(1 \/ var\(--ui-zoom, 1\)\)/);
  });
  it("take the library view's sizes (no sizes of their own in the standard and amp views)", () => {
    const parts = /(\.nav|\.nav-icon|\.queue-panel h2|\.queue-count|\.queue-list li|\.folder-list li|\.folders-head|\.folder-count|\.open-file|\.view-switch)(\s*,|\s*$)/;
    const own = all.filter(
      (r) =>
        /\[data-(look="real"|ui-view="amp")\]/.test(r.selector) &&
        r.selector.split(",").some((s) => parts.test(s.trim()) && /\[data-(look|ui-view)/.test(s)) &&
        /(font-size|(^|[^-])height|padding):/.test(r.body),
    );
    expect(own.map((r) => r.selector)).toEqual([]);
  });
});

describe("見た目's two boxes (task 6-10, 2026-11-15)", () => {
  it("have room between them", () => {
    expect(all.find((r) => r.selector === "#settings .themes + .themes")?.body).toMatch(/margin-top:\s*\d+px/);
  });
});
