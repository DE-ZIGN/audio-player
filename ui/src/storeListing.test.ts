import { describe, expect, it } from "vitest";
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
