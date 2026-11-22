import { describe, expect, it } from "vitest";
import page from "../index.html?raw";
import { en as pageEn, ja as pageJa } from "./i18n/parts/page";
import { en as statusEn, ja as statusJa } from "./i18n/parts/status";

// The texts that give measured values say no more than was measured on
// 2026-11-22 (docs/phase1-tasks.md, "審査の取り消しと点検"). The measurements
// themselves are held by tests/unit/resampler_test.cpp and tube_test.cpp.

describe("texts with measured values", () => {
  it("the resampler: 142 dB over the rates' sweep, not 200", () => {
    for (const s of [pageJa["page.settings.rate.note"], pageEn["page.settings.rate.note"]]) {
      expect(s).not.toMatch(/200 dB/);
      expect(s).toMatch(/142 dB/);
      expect(s).toMatch(/90/); // the pairs of rates swept
    }
  });

  it("PP CLASS A: the least harmonics only up to DRIVE -6 dB, as measured at 1 kHz", () => {
    expect(statusJa["status.tube.notePpclassa"]).toMatch(/1 kHz.*DRIVE −6 dB 以下.*最少/);
    expect(statusEn["status.tube.notePpclassa"]).toMatch(/1 kHz.*DRIVE −6 dB and below.*least/);
  });

  it("the tube stage: about 0.5 dB down at 20 kHz from 44.1 to 96 kHz", () => {
    expect(pageJa["page.settings.tube.note"]).toMatch(/44\.1〜96 kHz.*20 kHz.*0\.5 dB/);
    expect(pageEn["page.settings.tube.note"]).toMatch(/44\.1 to 96 kHz.*20 kHz.*0\.5 dB/);
  });

  it("the page's own Japanese is the table's", () => {
    expect(page).toContain(pageJa["page.settings.tube.note"]);
    expect(page).toContain(pageJa["page.settings.rate.note"]);
  });
});
