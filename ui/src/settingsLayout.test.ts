import { describe, expect, it } from "vitest";
import html from "../index.html?raw";

// Task 6-10: the settings screen opens with its explanations folded behind
// a 詳細 button (an accordion), so it is not a wall of text at first sight.
// What must be seen without opening anything stays out: the controls, the
// current state (the notes with an id, filled in by settingsView.ts), the
// cautions, and the lines that keep a show from claiming too much.

const start = html.indexOf('<section class="settings" id="settings"');
const settings = html.slice(start, html.indexOf("</section>", start));

interface Note {
  attrs: string;
  text: string;
  inDetails: boolean; // inside a <details> (closed at first)
}

// The <p> paragraphs of the settings screen, and whether each is folded.
function notes(): Note[] {
  const out: Note[] = [];
  let depth = 0;
  const tag = /<(\/?)(details|p)\b([^>]*)>/g;
  let m: RegExpExecArray | null;
  while ((m = tag.exec(settings))) {
    const [, close, name, attrs] = m;
    if (name === "details") depth += close ? -1 : 1;
    else if (!close) {
      const end = settings.indexOf("</p>", tag.lastIndex);
      out.push({ attrs, text: settings.slice(tag.lastIndex, end).replace(/\s+/g, " ").trim(), inDetails: depth > 0 });
    }
  }
  return out;
}

const plainNote = (n: Note) => /class="note"/.test(n.attrs) && !/\bid=/.test(n.attrs);

describe("the settings screen's explanations (task 6-10)", () => {
  it("folds every explanation behind a 詳細 button", () => {
    const unfolded = notes().filter((n) => plainNote(n) && !n.inDetails);
    expect(unfolded.map((n) => n.text.slice(0, 30))).toEqual([]);
    const explains = settings.match(/<details class="explain">\s*<summary>([^<]*)<\/summary>/g) ?? [];
    expect(explains.length).toBeGreaterThanOrEqual(10);
    for (const d of explains) expect(d).toMatch(/<summary>詳細<\/summary>/);
  });

  it("keeps the current state in sight (the notes the screen fills in)", () => {
    const ids = ["mode-summary", "eq-preamp", "tube-type-note", "tube-warmup-now", "tube-unit-now", "tube-auto-now", "space-room-note"];
    for (const id of ids) {
      const n = notes().find((x) => x.attrs.includes(`id="${id}"`));
      expect(n, id).toBeDefined();
      expect(n!.inDetails, id).toBe(false);
    }
  });

  it("keeps the cautions and what a show does not claim in sight", () => {
    const visible = notes()
      .filter((n) => !n.inDetails)
      .map((n) => n.text)
      .join("\n");
    expect(visible).toContain("対応していない DAC では大きな雑音になります"); // DoP
    expect(visible).toContain("再生中は一瞬途切れます"); // the tube on or off
    expect(visible).toContain("ジャンルに合う種類を示すものではありません"); // the tube by genre
    expect(settings).toMatch(/<h3>演出 <small>ロマン枠：効果は確かめていません<\/small><\/h3>/);
  });
});

describe("the settings screen's words and layout (task 6-10, 2026-11-15)", () => {
  it("calls the space's low cut ローカット", () => {
    expect(settings).toMatch(/<label class="slider-row" for="space-lowcut">ローカット/);
    expect(settings).not.toContain("響きの低域カット");
  });
  it("names the views on two lines, side by side as before", () => {
    const themes = settings.slice(settings.indexOf('<fieldset class="themes">'));
    for (const [value, name, sub] of [
      ["standard", "標準ビュー", "写実的メディアプレイヤー"],
      ["amp", "アンプビュー", "真空管アンプ風。情報欄は小さめ"],
      ["library", "ライブラリビュー", "ライブラリ型メディアプレイヤー風"],
    ])
      expect(themes).toMatch(
        new RegExp(`<input type="radio" name="ui-view" value="${value}" />\\s*<span><b>${name}</b><small>${sub}</small></span>`),
      );
  });
});
