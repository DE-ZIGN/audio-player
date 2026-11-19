import { describe, expect, it } from "vitest";
import { savedSettingsTab, SETTINGS_TABS, tabByKey, tabOfSection } from "./settingsTabs";
import { ARTICLE_SETTINGS_SECTIONS } from "./articleCommand";
import html from "../index.html?raw";
import css from "./style.css?raw";

// The settings screen as tabs (2026-11-18, the user): the tabs down the
// left, one tab's sections on the right, so every heading is in sight
// without scrolling to the end of one long page.

const start = html.indexOf('<section class="settings" id="settings"');
const settings = html.slice(start, html.indexOf("</section>", start));
// The groups in the page, in order: one per heading.
const groups = [...settings.matchAll(/<div class="settings-group" data-group="([a-z0-9-]+)">\s*(?:<!--[\s\S]*?-->\s*)?<(\w+)/g)].map((m) => ({
  name: m[1],
  first: m[2],
}));

describe("the settings screen's tabs", () => {
  it("every section of the page is in exactly one tab, and every tab's sections are in the page", () => {
    const inTabs = SETTINGS_TABS.flatMap((t) => t.sections);
    expect(new Set(inTabs).size).toBe(inTabs.length);
    expect([...inTabs].sort()).toEqual(groups.map((g) => g.name).sort());
    expect(groups.length).toBeGreaterThanOrEqual(12);
  });
  it("each group starts with its heading", () => {
    expect(groups.every((g) => g.first === "h2")).toBe(true);
    expect((settings.match(/<h2[\s>]/g) ?? []).length).toBe(groups.length); // no heading outside a group
  });
  it("the tabs have their own names, and there are few enough to see at once", () => {
    const ids = SETTINGS_TABS.map((t) => t.id);
    expect(new Set(ids).size).toBe(ids.length);
    expect(SETTINGS_TABS.every((t) => t.label.length > 0 && t.sections.length > 0)).toBe(true);
    expect(SETTINGS_TABS.length).toBeLessThanOrEqual(8);
  });
  it("a section's tab can be found (an article's link opens the right tab)", () => {
    expect(tabOfSection("eq")).toBe("eq");
    expect(tabOfSection("tone")).toBe("eq");
    expect(tabOfSection("output-rate")).toBe("output");
    expect(tabOfSection("no-such-section")).toBeNull();
    for (const s of ARTICLE_SETTINGS_SECTIONS) expect(tabOfSection(s), s).not.toBeNull();
  });
  it("the arrow keys go round the tabs; Home and End go to the ends", () => {
    const ids = SETTINGS_TABS.map((t) => t.id);
    expect(tabByKey(ids[0], "ArrowDown")).toBe(ids[1]);
    expect(tabByKey(ids[1], "ArrowUp")).toBe(ids[0]);
    expect(tabByKey(ids[0], "ArrowUp")).toBe(ids.at(-1));
    expect(tabByKey(ids.at(-1)!, "ArrowDown")).toBe(ids[0]);
    expect(tabByKey(ids[2], "Home")).toBe(ids[0]);
    expect(tabByKey(ids[2], "End")).toBe(ids.at(-1));
    expect(tabByKey(ids[2], "Enter")).toBeNull();
    expect(tabByKey("no-such-tab", "ArrowDown")).toBe(ids[0]);
  });
  it("opens on the tab last used, or the first", () => {
    expect(savedSettingsTab(null)).toBe(SETTINGS_TABS[0].id);
    expect(savedSettingsTab("tube")).toBe("tube");
    expect(savedSettingsTab("no-such-tab")).toBe(SETTINGS_TABS[0].id);
  });
});

describe("the page and its style", () => {
  it("has the list of tabs and the panel they show", () => {
    expect(settings).toMatch(/id="settings-tabs"[^>]*role="tablist"[^>]*aria-orientation="vertical"/);
    expect(settings).toMatch(/id="settings-panes"[^>]*role="tabpanel"/);
  });
  it("the tabs and the panel scroll on their own; the screen stays hidden when it is hidden", () => {
    const plain = css.replace(/\/\*[\s\S]*?\*\//g, "");
    const body = (sel: string) => new RegExp(`(?:^|\\})\\s*${sel.replace(/[.#[\]"=]/g, "\\$&")}\\s*\\{([^}]*)\\}`).exec(plain)?.[1] ?? "";
    expect(body(".settings")).toMatch(/display:\s*grid/);
    expect(body(".settings-tabs")).toMatch(/overflow-y:\s*auto/);
    expect(body(".settings-panes")).toMatch(/overflow-y:\s*auto/);
    expect(plain).toMatch(/\[hidden\]\s*\{[^}]*display:\s*none\s*!important/); // display: grid must not show a hidden screen
  });
  it("in the amp view's short panel the tabs go along the top, in a row", () => {
    const plain = css.replace(/\/\*[\s\S]*?\*\//g, "");
    const tabs = /\[data-ui-view="amp"\] \.settings-tabs\s*\{([^}]*)\}/.exec(plain)?.[1] ?? "";
    expect(tabs).toMatch(/flex-direction:\s*row/);
    expect(tabs).toMatch(/flex-wrap:\s*wrap/);
    expect(/\[data-ui-view="amp"\] \.settings\s*\{([^}]*)\}/.exec(plain)?.[1] ?? "").toMatch(/grid-template-columns:\s*minmax\(0, 1fr\)/);
  });
});

// The look chosen from three trials (2026-11-19, the user: "B"): the tabs are
// one panel, each with a lamp that is lit for the chosen one (as the amp's
// TUBE TYPE lamps); a heading is in the accent colour with a thin line that
// fades out to the right. The same in every view and theme, so no colour of
// one theme is written into these rules.
describe("the settings screen's look: a panel of lamps and lined headings", () => {
  const plain = css.replace(/\/\*[\s\S]*?\*\//g, "");
  const body = (sel: string) => new RegExp(`(?:^|\\})\\s*${sel.replace(/[.#[\]"=():*]/g, "\\$&")}\\s*\\{([^}]*)\\}`).exec(plain)?.[1] ?? "";
  it("the tabs are one panel, no taller than it needs, that still scrolls when the window is short", () => {
    const tabs = body(".settings-tabs");
    expect(tabs).toMatch(/align-self:\s*start/);
    expect(tabs).toMatch(/max-height:\s*100%/);
    expect(tabs).toMatch(/overflow-y:\s*auto/);
    expect(tabs).toMatch(/border-radius:/);
    expect(tabs).toMatch(/background:\s*linear-gradient/);
  });
  it("every tab has a lamp; the chosen tab's is lit in the accent, and the tab is bold (not colour alone)", () => {
    const lamp = body("#settings-tabs button::before");
    expect(lamp).toMatch(/content:\s*""/);
    expect(lamp).toMatch(/border-radius:\s*50%/);
    const lit = body('#settings-tabs button[aria-selected="true"]::before');
    expect(lit).toMatch(/background:\s*radial-gradient\([^;]*var\(--accent\)/);
    expect(lit).toMatch(/box-shadow:[^;]*var\(--accent\)/);
    expect(body('#settings-tabs button[aria-selected="true"]')).toMatch(/font-weight:\s*700/);
  });
  it("the tabs' colours are stated under #settings-tabs (the settings screen makes everything inherit its white)", () => {
    expect(body("#settings-tabs button")).toMatch(/color:\s*rgba\(255, 255, 255, 0\.78\)/);
    expect(body('#settings-tabs button[aria-selected="true"]')).toMatch(/color:\s*#fff/);
  });
  it("the tab the keyboard is on shows it, inside the panel's edge", () => {
    const focus = body("#settings-tabs button:focus-visible");
    expect(focus).toMatch(/outline:\s*2px solid var\(--accent\)/);
    expect(focus).toMatch(/outline-offset:\s*-/);
  });
  it("a heading is in the accent colour, with a line fading out to the right in place of the underline", () => {
    const h2 = body("#settings h2");
    expect(h2).toMatch(/color:\s*var\(--accent\)/);
    expect(h2).toMatch(/display:\s*flex/);
    expect(h2).toMatch(/border-bottom:\s*0;/);
    const line = body("#settings h2::after");
    expect(line).toMatch(/flex:\s*1/);
    expect(line).toMatch(/background:\s*linear-gradient\(90deg, var\(--accent\), transparent\)/);
    expect(body("#settings h2 small")).toMatch(/color:\s*#fff/); // "音作りモードでかけます" stays white
  });
  it("follows the theme: none of these rules names a theme's own colour", () => {
    const rules = [...plain.matchAll(/([^{}]+)\{([^}]*)\}/g)]
      .filter((m) => /#settings-tabs|\.settings-tabs|#settings h2/.test(m[1]))
      .map((m) => m[2])
      .join(" ");
    expect(rules).not.toMatch(/#f0a63a|#f4a640|#8fa8c4|240, 166, 58|244, 166, 64|143, 168, 196/i);
  });
  it("in the amp view the panel of tabs is a row, as wide as its tabs", () => {
    expect(body('[data-ui-view="amp"] .settings-tabs')).toMatch(/justify-self:\s*start/);
    expect(body('[data-ui-view="amp"] #settings-tabs button')).toMatch(/border-right:/);
  });
});
