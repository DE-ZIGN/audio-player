import { describe, expect, it } from "vitest";
import readme from "../../packaging/store/README.md?raw";
import policy from "../../packaging/store/privacy-policy.md?raw";
import listingRaw from "../../packaging/store/listing.json?raw";
import cmake from "../../CMakeLists.txt?raw";
import { en as aboutEn, ja as aboutJa } from "./i18n/parts/about";

// The Microsoft Store listing (task 8-5a): packaging/store/listing.json, one
// entry per language, pasted into Partner Center. The limits are Partner
// Center's (Microsoft Learn, "Create app Store listings", 2026-11-21); those
// marked "not on the page" are kept short until Partner Center shows its own.

type Listing = {
  productName: string;
  shortTitle: string;
  shortDescription: string[];
  description: string[];
  features: string[];
  screenshotCaptions: string[];
  searchTerms: string[];
  copyrightAndTrademark: string;
  additionalLicenseTerms: string[];
  developedBy: string;
};

const logos = Object.keys(import.meta.glob("../../packaging/store/logos/*.png"));
// A block quote under a heading of packaging/store/README.md: a text to paste.
const quoted = (heading: string) =>
  readme
    .split(/^### /m)
    .find((part) => part.startsWith(heading))!
    .split(/\r?\n/)
    .filter((line) => line.startsWith(">"))
    .map((line) => line.replace(/^> ?/, ""))
    .join("\n");
const screenshots = Object.keys(import.meta.glob("../../packaging/store/screenshots/*/*.png"));
const all = JSON.parse(listingRaw) as Record<string, unknown>;
const LANGS = ["ja-JP", "en-US"] as const;
const listing = (lang: (typeof LANGS)[number]) => all[lang] as Listing;
const text = (lines: string[]) => lines.join("\n");
const cached = (name: string) => new RegExp(`set\\(${name}\\s+"([^"]*)"\\s+CACHE STRING`).exec(cmake)?.[1];
const everything = (l: Listing) =>
  [
    l.productName,
    l.shortTitle,
    text(l.shortDescription),
    text(l.description),
    ...l.features,
    ...l.screenshotCaptions,
    ...l.searchTerms,
    l.copyrightAndTrademark,
    text(l.additionalLicenseTerms),
    l.developedBy,
  ].join("\n");

describe("the Store listing's languages", () => {
  it("are Japanese and English, nothing else", () => {
    expect(Object.keys(all).filter((k) => !k.startsWith("_"))).toEqual([...LANGS]);
  });

  it("have the same fields, none of them empty", () => {
    const ja = listing("ja-JP");
    const en = listing("en-US");
    expect(Object.keys(en).sort()).toEqual(Object.keys(ja).sort());
    for (const lang of LANGS)
      for (const [k, v] of Object.entries(listing(lang))) {
        const values = Array.isArray(v) ? v.filter((s) => s !== "") : [v];
        expect(values.length, `${lang} ${k}`).toBeGreaterThan(0);
        for (const s of values) expect(String(s).trim().length, `${lang} ${k}`).toBeGreaterThan(0);
      }
  });

  it("have as many features, captions and search terms in each language", () => {
    for (const k of ["features", "screenshotCaptions", "searchTerms"] as const)
      expect(listing("en-US")[k].length, k).toBe(listing("ja-JP")[k].length);
  });
});

describe("the Store listing's limits (Partner Center)", () => {
  for (const lang of LANGS) {
    const l = listing(lang);
    it(`${lang}: description up to 10,000 characters, short description up to 270 (all of it shown)`, () => {
      expect(text(l.description).length).toBeLessThanOrEqual(10000);
      expect(text(l.shortDescription).length).toBeLessThanOrEqual(270);
    });
    it(`${lang}: up to 20 features of up to 200 characters`, () => {
      expect(l.features.length).toBeLessThanOrEqual(20);
      for (const f of l.features) expect(f.length, f).toBeLessThanOrEqual(200);
      // Partner Center adds its own bullets.
      for (const f of l.features) expect(f, f).not.toMatch(/^[・•\-*]/);
    });
    it(`${lang}: up to 10 screenshot captions of up to 200 characters, short title up to 50`, () => {
      expect(l.screenshotCaptions.length).toBeLessThanOrEqual(10);
      for (const c of l.screenshotCaptions) expect(c.length, c).toBeLessThanOrEqual(200);
      expect(l.shortTitle.length).toBeLessThanOrEqual(50);
    });
    it(`${lang}: search terms kept short (not on the page: 7 terms, 30 characters each, 21 words)`, () => {
      expect(l.searchTerms.length).toBeLessThanOrEqual(7);
      for (const t of l.searchTerms) expect(t.length, t).toBeLessThanOrEqual(30);
      expect(l.searchTerms.join(" ").split(/\s+/).length).toBeLessThanOrEqual(21);
      expect(new Set(l.searchTerms.map((t) => t.toLowerCase())).size).toBe(l.searchTerms.length);
    });
    it(`${lang}: copyright up to 200 characters, developer up to 255, license terms up to 10,000 (not on the page)`, () => {
      expect(l.copyrightAndTrademark.length).toBeLessThanOrEqual(200);
      expect(l.developedBy.length).toBeLessThanOrEqual(255);
      expect(text(l.additionalLicenseTerms).length).toBeLessThanOrEqual(10000);
    });
  }
});

describe("the Store's policies (7.20, read 2026-11-22)", () => {
  it("10.2.4: what the app depends on is at the beginning of the description", () => {
    expect(listing("ja-JP").description[0]).toMatch(/^必要なもの：.*WebView2 ランタイム/);
    expect(listing("en-US").description[0]).toMatch(/^Requires .*WebView2 Runtime/);
  });

  it("10.2.4: the certification notes say about drivers, the WebView2 Runtime and the account", () => {
    const notes = quoted("審査の人への注記");
    expect(notes).toMatch(/does not depend on any non-Microsoft driver/);
    expect(notes).toMatch(/WebView2 Runtime/);
    expect(notes).toMatch(/individual developer/);
    expect(notes.length).toBeLessThanOrEqual(2000);
  });

  it("the reason for runFullTrust is short enough for a small box", () => {
    expect(quoted("制限付きの機能").length).toBeLessThanOrEqual(500);
  });

  it("10.5.1: the privacy policy says how the user sees and deletes what is kept, and how it is kept", () => {
    for (const heading of ["確認と削除", "保護", "Your controls: seeing and deleting it", "How it is protected"])
      expect(policy).toContain(`### ${heading}`);
  });

  it("a desktop app is answered as one that can reach personal information", () => {
    expect(readme).toMatch(/\| 個人情報を扱うか \| \*\*はい\*\*/);
  });

  it("the 1:1 app tile icon is there (strongly recommended)", () => {
    expect(logos.map((p) => p.split("/").pop())).toEqual(["app-tile-300.png"]);
  });
});

// The claims against what the app does (2026-11-22, after the first submission
// was taken back for saying more than that).
describe("the Store listing's claims", () => {
  const ja = listing("ja-JP");
  const en = listing("en-US");
  const line = (l: Listing, start: string) => l.description.find((d) => d.startsWith(start)) ?? "";

  it("ABX: the levels are matched, not always within 0.1 dB (a loud master keeps a difference)", () => {
    expect(everything(ja)).not.toMatch(/0\.1 dB 以内に|音量をそろえて/);
    expect(everything(en)).not.toMatch(/matched within 0\.1 dB|the levels matched/);
    expect(line(ja, "・ABX")).toMatch(/0\.1 dB を超え.*参考/);
    expect(line(en, "- An ABX")).toMatch(/over 0\.1 dB.*reference/);
  });

  it("Pure mode: untouched only at 100% volume with ISP-safe off; float sources may be dithered", () => {
    expect(line(ja, "・原音モード")).toMatch(/音量 100%.*ISP セーフがオフ/);
    expect(line(ja, "・原音モード")).toMatch(/MP3.*ディザ/);
    expect(line(en, "- Pure mode")).toMatch(/100%.*ISP-safe off/);
    expect(line(en, "- Pure mode")).toMatch(/MP3.*dither/);
    for (const s of [text(ja.shortDescription), ja.features[0]]) expect(s).toContain("音量 100%");
    for (const s of [text(en.shortDescription), en.features[0]]) expect(s).toContain("100% volume");
  });

  it("gapless: only between tracks of the same format", () => {
    for (const s of [...ja.description, ...ja.features].filter((s) => s.includes("ギャップレス")))
      expect(s).toContain("同じ形式");
    for (const s of [...en.description, ...en.features].filter((s) => /gapless/i.test(s)))
      expect(s).toContain("same format");
  });

  it("formats: AAC is .m4a, and DST-compressed DFF is not played", () => {
    expect(text(ja.description)).toMatch(/AAC（m4a/);
    expect(text(ja.description)).toContain("DST 圧縮");
    expect(text(en.description)).toMatch(/AAC \(m4a/);
    expect(text(en.description)).toContain("DST-compressed");
  });

  it("10.2.4: Media Foundation, absent from Windows N, is among what the app depends on", () => {
    for (const l of [ja, en]) expect(l.description[0]).toContain("Media Feature Pack");
    expect(quoted("審査の人への注記")).toContain("Media Feature Pack");
  });

  it("BIT-PERFECT: judged up to the driver", () => {
    expect(line(ja, "・BIT-PERFECT")).toContain("ドライバー");
    expect(line(en, "- BIT-PERFECT")).toContain("driver");
  });

  it("DoP: not detected, loud noise on a DAC without it (a matter of safety)", () => {
    expect(line(ja, "・DSD")).toMatch(/自動では分かりません.*大きな雑音/);
    expect(line(en, "- DSD")).toMatch(/cannot detect.*loud noise/);
    expect(quoted("審査の人への注記")).toMatch(/cannot detect.*loud noise/);
  });

  it("the notes: what Processing mode converts is the rate", () => {
    const notes = quoted("審査の人への注記");
    expect(notes).not.toContain("converts such a track");
    expect(notes).toMatch(/Processing mode \(Settings > Mode\) converts a rate/);
  });

  it("the notes: exclusive mode is needed, and Pure mode's exception is said", () => {
    const notes = quoted("審査の人への注記");
    expect(notes).not.toMatch(/any Windows output works|converts nothing/);
    expect(notes).toMatch(/allows exclusive mode/);
    expect(notes).toMatch(/MP3.*dither/);
  });
});

describe("the Store's screenshots (task 8-5d)", () => {
  it("are one for each caption, in each language, numbered in the captions' order", () => {
    for (const lang of LANGS) {
      const numbers = screenshots
        .filter((p) => p.includes(`/screenshots/${lang}/`))
        .map((p) => Number(p.split("/").pop()!.slice(0, 2)))
        .sort((a, b) => a - b);
      expect(numbers, lang).toEqual(listing(lang).screenshotCaptions.map((_, i) => i + 1));
    }
  });
});

describe("the Store listing's words", () => {
  it("name the product and the publisher as CMakeLists.txt does", () => {
    for (const lang of LANGS) {
      expect(listing(lang).productName, lang).toBe(cached("AP_DISPLAY_NAME"));
      expect(listing(lang).shortTitle, lang).toBe(cached("AP_DISPLAY_NAME"));
      expect(listing(lang).developedBy, lang).toBe(cached("AP_COMPANY_NAME"));
    }
  });

  it("never say the sound gets better (CLAUDE.md chapter 1)", () => {
    expect(everything(listing("ja-JP"))).not.toMatch(/良くな|高音質|音質(が|の)?(向上|改善)|原音に近|より自然/);
    expect(everything(listing("en-US"))).not.toMatch(
      /\b(improves?|improved|improving|improvement|better sound|sounds? better|enhances?|enhanced|enhancement|superior|purer|cleaner sound|warmer|richer|high[- ]fidelity|audiophile[- ]grade|accurate sound|true sound)\b/i,
    );
  });

  it("carry the ASIO trademark notice as the app's About tab writes it", () => {
    expect(listing("ja-JP").description).toContain(aboutJa["about.trademark.asio"]);
    expect(listing("en-US").description).toContain(aboutEn["about.trademark.asio"]);
  });

  it("give the license and the source code, the license words as the About tab has them", () => {
    for (const lang of LANGS) {
      const terms = text(listing(lang).additionalLicenseTerms);
      expect(terms, lang).toContain("https://github.com/DE-ZIGN/audio-player");
      expect(terms, lang).toContain("GNU Affero General Public License");
    }
    // The same, without the pointer to the license text the tab has beside it.
    const firstSentences = (s: string) => s.replace(/（くわしくはライセンス本文）/, "").replace(/ \(see the license text\)/, "");
    expect(listing("ja-JP").additionalLicenseTerms[0]).toBe(firstSentences(aboutJa["about.app.free"]));
    expect(listing("en-US").additionalLicenseTerms[0]).toBe(firstSentences(aboutEn["about.app.free"]));
  });

  it("use the glossary's English for the two modes (docs/i18n-glossary.md)", () => {
    const en = everything(listing("en-US"));
    expect(en).toContain("Pure mode");
    expect(en).toContain("Processing mode");
    expect(en).toContain("Just for fun: no effect verified");
  });

  it("the English has no Japanese in it", () => {
    expect(everything(listing("en-US"))).not.toMatch(/[぀-ヿ㐀-鿿！-｠]/);
  });
});
