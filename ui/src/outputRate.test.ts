import { describe, expect, it } from "vitest";
import { outputTags, processingText, stageText } from "./messages";
import { pathModel, verdictModel } from "./outputCheck";
import { outputRateNote, parseResampleDetail, rateText, resampleText } from "./outputRate";
import { defaultListening, modeSummary, type ListeningSettings } from "./settings";
import type { PlaybackStatus } from "./status";

// Task 10-4: the output rate on the settings screen and the conversion in the path display.

describe("rate texts", () => {
  it("names a rate", () => {
    expect(rateText(44100)).toBe("44.1 kHz");
    expect(rateText(96000)).toBe("96 kHz");
    expect(rateText(352800)).toBe("352.8 kHz");
    expect(rateText(705600)).toBe("705.6 kHz"); // task 10-5
    expect(rateText(768000)).toBe("768 kHz");
  });
  it("reads the resampler's detail", () => {
    expect(parseResampleDetail("44100;96000;setting")).toEqual({ from: 44100, to: 96000, reason: "setting" });
    expect(parseResampleDetail("88200;44100;unsupported")).toEqual({ from: 88200, to: 44100, reason: "unsupported" });
    expect(parseResampleDetail("x")).toBeNull();
    expect(parseResampleDetail(undefined)).toBeNull();
  });
  it("says what was converted and why", () => {
    expect(resampleText("44100;96000;setting")).toBe("44.1 kHz → 96 kHz（設定）");
    expect(resampleText("88200;44100;unsupported")).toBe("88.2 kHz → 44.1 kHz（DAC が 88.2 kHz を受け付けないため）");
  });
});

describe("the settings screen's note and the mode summary", () => {
  const l: ListeningSettings = { ...defaultListening, mode: "processed" };
  it("says what each choice does", () => {
    expect(outputRateNote(l)).toBe("曲のレートのまま出します（DAC が受け付けないレートの曲だけ、受け付けるレートへ変換します）。");
    expect(outputRateNote({ ...l, outputRateMode: "fixed", outputRateHz: 96000 })).toBe(
      "どの曲も 96 kHz にして出します（それより高い曲は下げます）。",
    );
    expect(outputRateNote({ ...l, outputRateMode: "atleast", outputRateHz: 96000 })).toBe(
      "96 kHz 未満の曲を 96 kHz に上げます。96 kHz 以上の曲はそのままです。",
    );
    expect(outputRateNote({ ...l, outputRateMode: "multiple", outputRateMultiple: 2 })).toBe(
      "曲のレートの 2 倍で出します（44.1 → 88.2 kHz、48 → 96 kHz）。DAC が受け付けないときは、受け付けるいちばん大きい倍数です。",
    );
  });
  it("names the choice in the mode summary, not the default", () => {
    expect(modeSummary(l)).not.toContain("出力レート");
    expect(modeSummary({ ...l, outputRateMode: "fixed", outputRateHz: 96000 })).toContain("出力レート（固定 96 kHz）");
    expect(modeSummary({ ...l, outputRateMode: "atleast", outputRateHz: 88200 })).toContain("出力レート（88.2 kHz 以上）");
    expect(modeSummary({ ...l, outputRateMode: "multiple", outputRateMultiple: 4 })).toContain("出力レート（曲の 4 倍）");
    expect(modeSummary({ ...l, mode: "original", outputRateMode: "fixed" })).not.toContain("出力レート");
  });
});

const fmt = (rate: number) => ({ sampleRate: rate, channels: 2, containerBits: 32, validBits: 32, sampleType: "int" as const });

const resampled = {
  mode: "processed",
  outputFormat: fmt(96000),
  sourceFormat: { ...fmt(44100), containerBits: 24, validBits: 24 },
  dop: null,
  volumeDb: 0,
  limiter: { events: 0, limitedFrames: 0, maxReductionDb: 0 },
  integrity: { state: "notAvailable", decodeErrors: 0 },
  stages: [
    { name: "limiter", gainDb: null },
    { name: "resampler", gainDb: null, detail: "44100;96000;setting" },
  ],
  path: {
    container: "FLAC",
    processing: true,
    processingStages: ["limiter", "resampler"],
    processingRequired: false,
    dither: true,
    comparison: false,
    output: { api: "dummy", periodFrames: 480, periodMilliseconds: 10, eventDriven: false, layout: "stereo" },
  },
  bitPerfect: { bitPerfect: false, stages: [], dither: true, comparison: false, quantized: false, dop: false },
} as unknown as PlaybackStatus;

describe("the conversion in the path display", () => {
  it("has its row in the processing box, after the limiter", () => {
    const rows = pathModel(resampled).processing.rows;
    const names = rows.map((r) => r.name);
    const i = names.indexOf("リサンプラー");
    expect(i).toBe(names.indexOf("保険のリミッター") + 1);
    expect(rows[i]).toMatchObject({ on: true, value: "44.1 kHz → 96 kHz（設定）" });
    // Not converting: OFF.
    const plain = { ...resampled, stages: [{ name: "limiter", gainDb: null }] } as unknown as PlaybackStatus;
    expect(pathModel(plain).processing.rows.find((r) => r.name === "リサンプラー")).toMatchObject({ on: false, value: "OFF" });
  });
  it("is named in the processing text and as a reason it is not bit-perfect", () => {
    expect(stageText({ name: "resampler", gainDb: null, detail: "44100;96000;setting" })).toBe(
      "リサンプラー（44.1 kHz → 96 kHz（設定））",
    );
    expect(processingText(resampled)).toContain("リサンプラー（44.1 kHz → 96 kHz（設定））");
    const check = verdictModel(resampled)!.checks.find((c) => c.label === "処理なし");
    expect(check?.detail).toContain("リサンプラー");
  });
  it("tags the output field when the conversion is all that runs", () => {
    expect(outputTags(resampled).map((t) => t.text)).toContain("レート変換");
  });
});
