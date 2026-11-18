import { describe, expect, it } from "vitest";
import { ARTICLE_SETTINGS_SECTIONS, articleScreen } from "./articleCommand";
import html from "../index.html?raw";
import policy from "../../app/logic/ArticlePolicy.cpp?raw";

// Task 7-1: a link in an article may ask the main screen to open one of
// three screens. The app has already checked the command (ArticlePolicy.h);
// the page checks again and does nothing with what it does not know.

describe("a command from an article", () => {
  it("opens the ABX, the output check or the settings", () => {
    expect(articleScreen({ command: "abx", argument: "" })).toEqual({ screen: "abx" });
    expect(articleScreen({ command: "output-check", argument: "" })).toEqual({ screen: "output-check" });
    expect(articleScreen({ command: "settings", argument: "" })).toEqual({ screen: "settings", section: null });
    expect(articleScreen({ command: "settings", argument: "eq" })).toEqual({ screen: "settings", section: "eq" });
    expect(articleScreen({ command: "settings" })).toEqual({ screen: "settings", section: null });
  });
  it("anything else does nothing", () => {
    for (const p of [
      null,
      undefined,
      "abx",
      {},
      { command: "play" },
      { command: "set-mode", argument: "tube" },
      { command: "abx", argument: "now" },
      { command: "output-check", argument: "1" },
      { command: "settings", argument: "unknown" },
      { command: "settings", argument: 3 },
      { command: "ABX" },
    ]) {
      expect(articleScreen(p), JSON.stringify(p)).toBeNull();
    }
  });
});

describe("the settings sections an article may open", () => {
  it("are the ones the app allows (ArticlePolicy.cpp)", () => {
    const list = /kSettingsSections\{([^}]*)\}/.exec(policy)?.[1] ?? "";
    const inApp = [...list.matchAll(/"([a-z0-9-]+)"/g)].map((m) => m[1]);
    expect(inApp.length).toBeGreaterThan(0);
    expect([...ARTICLE_SETTINGS_SECTIONS]).toEqual(inApp);
  });
  it("each is a heading of the settings screen", () => {
    const start = html.indexOf('<section class="settings" id="settings"');
    const settings = html.slice(start, html.indexOf("</section>", start));
    const marked = [...settings.matchAll(/<h2 data-section="([a-z0-9-]+)"/g)].map((m) => m[1]);
    expect(marked).toEqual([...ARTICLE_SETTINGS_SECTIONS]);
  });
});
