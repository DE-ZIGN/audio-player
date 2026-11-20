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
  it("is at its real size in the zoomed views (with the rest of the middle panel, 2026-11-18)", () => {
    expect(body("#settings")).toMatch(/--spx:\s*1px/);
    // The panel's contents are shown at the real size as a whole; the settings set no scale of their own.
    expect(all.filter((r) => /\[data-look="real"\]/.test(r.selector) && /--spx:/.test(r.body))).toEqual([]);
    const r = all.find((x) => x.selector === '[data-look="real"] :is(.sidebar, .queue-panel, .content) > *');
    expect(r?.body).toMatch(/zoom:\s*calc\(1 \/ var\(--ui-zoom, 1\)\)/);
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
    // The words come from the tables (task 11-4): a CSS variable the page sets (i18n.ts CSS_TEXTS).
    expect(body(".settings.original-mode .for-mode::after")).toMatch(/content:\s*var\(--i18n-for-mode-off\)/);
  });
  it("has headings that read as headings, room before each, and looser lines", () => {
    // 18 px and wide-set since 2026-11-19 (the look the user chose; 20 px before).
    expect(body("#settings h2")).toMatch(/font-size:\s*calc\(18 \* var\(--spx\)\)/);
    expect(body("#settings h2")).toMatch(/font-weight:\s*700/);
    expect(body("#settings h2")).toMatch(/margin:\s*44px 0 14px/);
    expect(body("#settings h2")).toMatch(/letter-spacing:\s*0\.14em/);
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
    const r = all.find((x) => x.selector === '[data-look="real"] :is(.sidebar, .queue-panel, .content) > *');
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

describe("an album in the amp view (2026-11-15, the user)", () => {
  // The panel under the amp is shorter than the album's head: the whole panel
  // scrolls, the head keeps its size, and the track list fills the panel
  // once the head has scrolled away.
  const body = (sel: string) => all.find((r) => r.selector === sel)?.body ?? "";
  const open = '[data-ui-view="amp"] .content:has(> #album-head:not([hidden]))';
  it("scrolls as a whole", () => {
    expect(body('[data-ui-view="amp"] .content')).toMatch(/container-type:\s*size/);
    expect(body(open)).toMatch(/overflow-y:\s*auto/);
  });
  it("keeps the head (and its cover) whole, and gives the list the panel's height", () => {
    expect(body(`${open} > #album-head`)).toMatch(/flex-shrink:\s*0/);
    expect(body(`${open} > #table`)).toMatch(/height:\s*calc\(100cqh \* var\(--ui-zoom, 1\) - var\(--amp-now-line\)\)/); // the panel in zoomed px, its contents at the real size
  });
});

describe("the middle panel in the zoomed views (2026-11-18, the user)", () => {
  // The heading, the search, the track list, the album tiles and an album's
  // head: at their real size, the same as the library view's.
  it("takes the library view's sizes (no type or row sizes of its own in the standard and amp views)", () => {
    const parts =
      /(\.content-head h1|#search|#sort|\.view-toggle button|\.table-head|\.table-head button|\.row \.mark|\.tile-title|\.tile-sub|\.album-cover|\.album-info h2|\.album-artist|\.album-meta|\.album-meta \.tag)(\s*,|\s*$)/;
    const own = all.filter(
      (r) =>
        /\[data-look="real"\]/.test(r.selector) &&
        r.selector.split(",").some((s) => parts.test(s.trim())) &&
        /(font-size|(^|[^-])(width|height)):/.test(r.body),
    );
    expect(own.map((r) => r.selector)).toEqual([]);
  });
  it("uses the library view's rows and tiles", () => {
    const real = all.find((r) => r.selector === ':root[data-look="real"]')?.body ?? "";
    expect(real).not.toMatch(/--row-h:|--tile-min:|--tile-text:/);
    expect(real).toMatch(/--sidebar-w:/); // the panels themselves keep the concept image's sizes
  });
});
