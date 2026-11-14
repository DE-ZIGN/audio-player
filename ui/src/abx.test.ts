import { describe, expect, it } from "vitest";
import { defaultListening } from "./settings";
import {
  defaultRegion,
  keyAction,
  lowerTailP,
  defaultSides,
  sidePairProblem,
  sideRequest,
  regionText,
  resultText,
  sideLabels,
  upperTailP,
  type AbxResultInfo,
} from "./abx";

const finished = (correct: number, trials = 16, over: Partial<AbxResultInfo> = {}): AbxResultInfo => ({
  finished: true,
  aborted: false,
  trials,
  correct,
  pValue: upperTailP(correct, trials),
  distinguished: upperTailP(correct, trials) < 0.05,
  levelDifferenceDb: 0.004,
  levelMatched: true,
  ...over,
});

describe("binomial tails", () => {
  it("the same p values as the engine (task 4-1)", () => {
    expect(upperTailP(12, 16)).toBeCloseTo(2517 / 65536, 12);
    expect(upperTailP(8, 16)).toBeCloseTo(39203 / 65536, 12);
    expect(upperTailP(16, 16)).toBeCloseTo(1 / 65536, 12);
    expect(upperTailP(0, 16)).toBe(1);
  });
  it("the lower tail is the upper tail mirrored", () => {
    expect(lowerTailP(4, 16)).toBeCloseTo(2517 / 65536, 12);
    expect(lowerTailP(5, 16)).toBeCloseTo(6885 / 65536, 12);
    expect(lowerTailP(16, 16)).toBe(1);
  });
});

describe("result texts", () => {
  it("distinguished at p < 0.05: unlikely to be chance, nothing about which is better", () => {
    const t = resultText(finished(12));
    expect(t.headline).toBe("16 回中 12 回正解");
    expect(t.p).toBe("p = 0.0384（当てずっぽうでこれ以上当たる確率）");
    expect(t.verdict).toContain("偶然とは考えにくい");
    expect(t.verdict).not.toMatch(/良い|よい|優れ/);
    expect(t.low).toBe("");
  });
  it("not distinguished: not shown, which is not the same as no difference", () => {
    const t = resultText(finished(11));
    expect(t.verdict).toContain("聞き分けられたとは言えません");
    expect(t.verdict).toContain("違いがないという意味ではありません");
  });
  it("too few correct: a separate note that A and B may be swapped, not counted as distinguished", () => {
    const t = resultText(finished(4));
    expect(t.verdict).toContain("聞き分けられたとは言えません");
    expect(t.low).toContain("3.8%");
    expect(t.low).toContain("取り違えている可能性");
    expect(resultText(finished(5)).low).toBe("");
  });
  it("the loudness check: within 0.1 dB, over it (only for reference), not measured", () => {
    expect(resultText(finished(9)).level).toBe("音量差 +0.00 dB（基準は 0.1 dB 以内）");
    const over = resultText(finished(9, 16, { levelDifferenceDb: -0.15, levelMatched: false })).level;
    expect(over).toContain("−0.15 dB");
    expect(over).toContain("結果は参考");
    expect(resultText(finished(9, 16, { levelDifferenceDb: null })).level).toContain("測れませんでした");
  });
  it("aborted: no result", () => {
    const t = resultText({ ...finished(3), finished: false, aborted: true, correct: null, pValue: null });
    expect(t.headline).toBe("中断しました");
    expect(t.verdict).toContain("結果は出しません");
    expect(t.p).toBe("");
  });
});

describe("region and keys", () => {
  it("the region text", () => {
    expect(regionText(30 * 48000, 30 * 48000, 48000)).toBe("0:30〜1:00（30 秒）をくり返し");
  });
  it("the region starts at the current position, moved back so it fits the track", () => {
    expect(defaultRegion(45, 300, 30)).toEqual({ start: 45, length: 30 });
    expect(defaultRegion(290, 300, 30)).toEqual({ start: 270, length: 30 });
    expect(defaultRegion(5, 12, 30)).toEqual({ start: 0, length: 12 });
  });
  it("keys: a b x to listen, 1 2 to answer, Esc or q to stop", () => {
    expect(keyAction("a")).toBe("A");
    expect(keyAction("X")).toBe("X");
    expect(keyAction("1")).toBe("answerA");
    expect(keyAction("2")).toBe("answerB");
    expect(keyAction("Escape")).toBe("abort");
    expect(keyAction("q")).toBe("abort");
    expect(keyAction("Enter")).toBeNull();
  });
});

describe("設定 A と設定 B (task 6-9)", () => {
  it("names the sides after the presets", () => {
    const l = sideLabels({ kind: "presets", aName: "EQ なし（平ら）", bName: "HD 600" });
    expect(l.a).toBe("EQ なし（平ら）");
    expect(l.b).toBe("HD 600");
    expect(l.title).toBe("設定 A「EQ なし（平ら）」と 設定 B「HD 600」");
  });
  it("keeps the fixed names for the other kinds", () => {
    expect(sideLabels({ kind: "processed" })).toEqual({ a: "原音", b: "音作り", title: "原音 と 音作り（今の設定）" });
    expect(sideLabels({ kind: "16bit" }).b).toBe("16bit");
    expect(sideLabels({ kind: "presets" }).a).toBe("設定 A"); // names not known yet
  });
  it("two different settings are needed", () => {
    const side = { eq: "current", tone: "current" as const, tube: "se300b" as const, drive: -12 };
    expect(sidePairProblem(side, { ...side })).toContain("違う");
    expect(sidePairProblem(side, { ...side, tube: "ppel34" })).toBe("");
    expect(sidePairProblem(side, { ...side, drive: -6 })).toBe("");
    // Without the tube, DRIVE does not matter.
    expect(sidePairProblem({ ...side, tube: "none" }, { ...side, tube: "none", drive: 0 })).toContain("違う");
  });
});

describe("設定 A と設定 B with the tone and the tube (task 9-4)", () => {
  const listening = {
    ...defaultListening,
    mode: "processed" as const,
    eqBands: [{ type: "peak" as const, frequency: 1000, gainDb: 3, q: 1, enabled: true }],
    toneBassDb: 2,
    tubeEnabled: true,
    tubeType: "se300b" as const,
    tubeDriveDb: -10,
  };
  const presets = [{ name: "HD 600", bands: [], autoPreamp: false, manualPreampDb: -2 }];

  it("by default compares the current settings with another tube type (or the tube on and off)", () => {
    const { a, b } = defaultSides(listening);
    expect(a).toEqual({ eq: "current", tone: "current", tube: "se300b", drive: -10 });
    expect(b).toEqual({ eq: "current", tone: "current", tube: "ppel34", drive: -10 });
    const off = defaultSides({ ...listening, tubeEnabled: false });
    expect(off.a.tube).toBe("none");
    expect(off.b.tube).toBe("se300b");
  });

  it("turns a side into what the app takes, with a name for it", () => {
    const r = sideRequest({ eq: "current", tone: "current", tube: "ppclassa", drive: -6 }, listening, presets);
    expect(r).toMatchObject({
      bands: listening.eqBands,
      toneBassDb: 2,
      toneTrebleDb: 0,
      tubeEnabled: true,
      tubeType: "ppclassa",
      tubeDriveDb: -6,
    });
    expect(r.name).toBe("今の EQ・トーン（BASS +2.0 dB）・PP CLASS A（DRIVE -6.0 dB）");
    const flat = sideRequest({ eq: "flat", tone: "flat", tube: "none", drive: -12 }, listening, presets);
    expect(flat).toMatchObject({ bands: [], toneBassDb: 0, toneTrebleDb: 0, tubeEnabled: false });
    expect(flat.name).toBe("EQ なし・真空管なし");
    const preset = sideRequest({ eq: "p:HD 600", tone: "flat", tube: "none", drive: -12 }, listening, presets);
    expect(preset).toMatchObject({ bands: [], autoPreamp: false, manualPreampDb: -2 });
    expect(preset.name).toBe("HD 600・真空管なし");
  });
});
