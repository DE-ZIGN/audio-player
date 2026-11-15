import { describe, expect, it } from "vitest";
import { defaultListening, modeSummary, type ListeningSettings } from "./settings";
import {
  WARMUP_SECONDS,
  characterParts,
  coldShare,
  glowOf,
  parseTubeDetail,
  tubeUnitFor,
  unitChannelsText,
  warmUpText,
} from "./tubeCharacter";

// Task 9-8: the tube's warm-up and the tubes' differences (a show).

describe("tubeUnitFor", () => {
  it("gives the engine's numbers (tests/unit/tube_character_test.cpp)", () => {
    const cases: [number, "se300b" | "ppel34" | "ppclassa", number, number, number, number][] = [
      [1, "se300b", 0, 0, 1.0835311788929731, 0.9672089572470335],
      [1, "se300b", 1, 0, 1.082577778819385, 1.0279958130206572],
      [42, "ppel34", 0, 1, 1.0412205401450092, 1.0071347847524539],
      [99, "ppclassa", 1, 1, 1.0071278280664373, 1.0315478512907519],
    ];
    for (const [n, type, ch, tube, emission, mu] of cases) {
      const u = tubeUnitFor(n, type, ch, tube);
      expect(u.emission).toBe(emission);
      expect(u.mu).toBe(mu);
    }
  });
  it("keeps the number within 1 .. 99", () => {
    expect(tubeUnitFor(0, "se300b", 0, 0)).toEqual(tubeUnitFor(1, "se300b", 0, 0));
    expect(tubeUnitFor(500, "se300b", 0, 0)).toEqual(tubeUnitFor(99, "se300b", 0, 0));
  });
});

describe("the warm-up", () => {
  it("falls like the engine's: 1 at switch-on, 0 from 20 minutes", () => {
    expect(coldShare(0)).toBe(1);
    expect(coldShare(WARMUP_SECONDS)).toBe(0);
    expect(coldShare(240)).toBeGreaterThan(0.3);
    expect(coldShare(240)).toBeLessThan(0.4);
  });
  it("says how long since switch-on and how long is left", () => {
    expect(warmUpText(185)).toBe("ウォームアップ中：起動から 3 分（暖まりきるまで、あと約 17 分）");
    expect(warmUpText(1199)).toBe("ウォームアップ中：起動から 19 分（暖まりきるまで、あと約 1 分）");
    expect(warmUpText(1200)).toBe("暖まった状態（起動から 20 分を過ぎました）");
  });
  it("lights the amp's tubes dimmer while cold", () => {
    expect(glowOf(null)).toBe(1);
    expect(glowOf(0)).toBeCloseTo(0.55, 9);
    expect(glowOf(WARMUP_SECONDS)).toBe(1);
  });
});

describe("parseTubeDetail", () => {
  it("reads the type, the warm-up time and the unit", () => {
    expect(parseTubeDetail("ppel34")).toEqual({ type: "ppel34", warmUpSeconds: null, unit: null });
    expect(parseTubeDetail("se300b;warmup=185;unit=12")).toEqual({ type: "se300b", warmUpSeconds: 185, unit: 12 });
    expect(parseTubeDetail("ppclassa;unit=3")).toEqual({ type: "ppclassa", warmUpSeconds: null, unit: 3 });
    expect(parseTubeDetail("kt88")).toEqual({ type: null, warmUpSeconds: null, unit: null });
    expect(parseTubeDetail(undefined)).toEqual({ type: null, warmUpSeconds: null, unit: null });
  });
  it("names them for the path display", () => {
    expect(characterParts(185, 12)).toEqual(["ウォームアップ中（起動から 3 分）", "球 No.12"]);
    expect(characterParts(1300, null)).toEqual(["暖まった状態"]);
    expect(characterParts(null, null)).toEqual([]);
  });
});

describe("unitChannelsText", () => {
  it("says each channel's tubes, as departures from the type's figures", () => {
    expect(unitChannelsText(1, "se300b")).toBe("左：エミッション +8.4%・μ −3.3%、右：エミッション +8.3%・μ +2.8%");
    const pp = unitChannelsText(42, "ppel34");
    expect(pp.startsWith("左：")).toBe(true);
    expect(pp).toContain("と"); // two tubes a channel
  });
});

describe("modeSummary with the show", () => {
  const l: ListeningSettings = { ...defaultListening, mode: "processed", tubeEnabled: true, tubeType: "ppel34", tubeDriveDb: -6 };
  it("names the warm-up and the unit with the tube", () => {
    expect(modeSummary(l)).toContain("真空管（PP EL34、DRIVE -6.0 dB）");
    expect(modeSummary({ ...l, tubeWarmUp: true, tubeUnits: true, tubeUnit: 7 })).toContain(
      "真空管（PP EL34、DRIVE -6.0 dB、ウォームアップの演出、球 No.7）",
    );
  });
});
