import { describe, expect, it } from "vitest";
import { barPercent, ispWarning, meterModel, pathModel, verdictModel } from "./outputCheck";
import { emptyStatus, type FormatInfo, type PlaybackStatus, type SignalPath } from "./status";

const f = (rate: number, container: number, valid: number, type: "int" | "float" = "int"): FormatInfo => ({
  sampleRate: rate,
  channels: 2,
  containerBits: container,
  validBits: valid,
  sampleType: type,
});

const path = (over: Partial<SignalPath> = {}, output: Partial<SignalPath["output"]> = {}): SignalPath => ({
  container: "FLAC",
  processing: false,
  processingStages: [],
  processingRequired: false,
  dither: false,
  comparison: false,
  ...over,
  output: {
    api: "wasapiExclusive",
    periodFrames: 960,
    periodMilliseconds: 10,
    eventDriven: true,
    layout: "interleaved",
    asio: null,
    endpointVolume: { scalar: 1, muted: false, hardware: true },
    ...output,
  },
});

const verdict = {
  bitPerfect: true,
  quantized: false,
  dither: false,
  comparison: false,
  repacked: true,
  dop: false,
  floatSource: false,
  dummyOutput: false,
  deviceVolume: null,
  deviceMuted: false,
  deliveryMismatch: false,
};

// A 24/96 FLAC on the direct path, repacked into a 32-bit container.
const direct: PlaybackStatus = {
  ...emptyStatus,
  state: "playing",
  fileName: "a.flac",
  sourceFormat: f(96000, 24, 24),
  outputFormat: f(96000, 32, 24),
  repack: "widerContainer",
  outputName: "スピーカー (FiiO KA11)",
  path: path(),
  bitPerfect: verdict,
  delivery: { matchedFrames: 96000 * 65, mismatchedFrames: 0 },
  integrity: { state: "passed", decodeErrors: 0 },
};

// The same, in 音作りモード with an EQ and the volume down.
const processed: PlaybackStatus = {
  ...direct,
  mode: "processed",
  ispSafe: "measured",
  volumeDb: -6,
  outputFormat: f(96000, 32, 32),
  repack: "none",
  path: path({ processing: true, processingStages: ["eq", "volume", "isp-safe", "limiter"], dither: true }),
  stages: [
    { name: "eq", gainDb: -3.2 },
    { name: "volume", gainDb: -6 },
    { name: "isp-safe", gainDb: -0.4 },
    { name: "limiter", gainDb: null },
  ],
  limiter: { events: 3, limitedFrames: 120, maxReductionDb: -0.8 },
  bitPerfect: { ...verdict, bitPerfect: false, dither: true, repacked: false },
  delivery: null,
};

describe("signal path diagram", () => {
  it("the direct path: file, decoder, BYPASS, the output and the DAC", () => {
    const m = pathModel(direct);
    expect(m.file.lines).toEqual(["FLAC", "24bit / 96 kHz", "MD5 一致"]);
    expect(m.decoder.lines).toEqual(["libFLAC", "整数のまま"]);
    expect(m.processing.badge).toBe("BYPASS（原音経路）");
    expect(m.processing.active).toBe(false);
    expect(m.processing.rows.every((r) => !r.on)).toBe(true);
    expect(m.processing.rows.map((r) => r.name)).toEqual([
      "ReplayGain",
      "EQ",
      "トーン",
      "真空管",
      "クロスフィード",
      "デジタル音量",
      "ISP セーフ",
      "保険のリミッター",
      "ディザ",
    ]);
    expect(m.output.lines).toEqual(["WASAPI 排他（イベント駆動）", "24bit（32bit コンテナ）/ 96 kHz", "詰め替え（値は同じ）"]);
    expect(m.dac.lines).toEqual(["スピーカー (FiiO KA11)"]);
  });
  it("the processing path: which stages run, with their values and the limiter's count", () => {
    const m = pathModel(processed);
    expect(m.processing.badge).toBe("64bit 浮動小数点");
    expect(m.processing.active).toBe(true);
    const row = (name: string) => m.processing.rows.find((r) => r.name === name)!;
    expect(row("ReplayGain")).toMatchObject({ on: false, value: "OFF" });
    expect(row("EQ")).toMatchObject({ on: true, value: "プリアンプ −3.2 dB" });
    expect(row("デジタル音量")).toMatchObject({ on: true, value: "−6.0 dB" });
    expect(row("ISP セーフ")).toMatchObject({ on: true, value: "−0.4 dB" });
    expect(row("保険のリミッター")).toMatchObject({ on: true, value: "3 回・最大 −0.8 dB", warn: true });
    expect(row("ディザ")).toMatchObject({ on: true, value: "TPDF" });
    expect(m.output.lines[1]).toBe("32bit / 96 kHz");
  });
  it("the tone and the tube (task 9-3): the tone's preamp, the tube's type and DRIVE", () => {
    const s: PlaybackStatus = {
      ...processed,
      stages: [
        ...processed.stages,
        { name: "tone", gainDb: -2, detail: "" },
        { name: "tube", gainDb: -12, detail: "ppel34" },
        { name: "crossfeed", gainDb: null, detail: "cmoy" },
      ],
    };
    const m = pathModel(s);
    const row = (name: string) => m.processing.rows.find((r) => r.name === name)!;
    expect(row("トーン")).toMatchObject({ on: true, value: "プリアンプ −2.0 dB" });
    expect(row("真空管")).toMatchObject({ on: true, value: "PP EL34・DRIVE −12.0 dB" });
    expect(row("クロスフィード")).toMatchObject({ on: true, value: "700 Hz・6.0 dB" });
    // Their order follows the chain: EQ, tone, tube, volume.
    const names = m.processing.rows.map((r) => r.name);
    expect(names.indexOf("トーン")).toBe(names.indexOf("EQ") + 1);
    expect(names.indexOf("真空管")).toBe(names.indexOf("トーン") + 1);
  });
  it("the tube type by genre (task 9-7): says what chose the type", () => {
    const s: PlaybackStatus = {
      ...processed,
      stages: [...processed.stages, { name: "tube", gainDb: -12, detail: "se300b" }],
      tubeAuto: { album: null, genres: ["Jazz"], choice: { type: "se300b", reason: "genre", matched: "Jazz" } },
    };
    const row = (st: PlaybackStatus) => pathModel(st).processing.rows.find((r) => r.name === "真空管")!;
    expect(row(s).value).toBe("SE 300B（ジャンル：Jazz）・DRIVE −12.0 dB");
    const album: PlaybackStatus = {
      ...s,
      tubeAuto: { album: { key: "k", title: "W" }, genres: [], choice: { type: "se300b", reason: "album", matched: "W" } },
    };
    expect(row(album).value).toBe("SE 300B（アルバムの指定）・DRIVE −12.0 dB");
    expect(row({ ...s, tubeAuto: { album: null, genres: [], choice: null } }).value).toBe("SE 300B・DRIVE −12.0 dB");
  });
  it("DoP: the DSD file, no processing", () => {
    const s: PlaybackStatus = {
      ...direct,
      fileName: "a.dsf",
      sourceFormat: f(176400, 24, 24),
      outputFormat: f(176400, 32, 24),
      dop: { dsdRate: 2822400, container: "DSF" },
      path: path({ container: "DSF" }),
      bitPerfect: { ...verdict, dop: true },
    };
    const m = pathModel(s);
    expect(m.file.lines.slice(0, 2)).toEqual(["DSF", "DSD64（2.8224 MHz）"]);
    expect(m.decoder.lines).toEqual(["自前", "DoP に包む（値はそのまま）"]);
    expect(m.processing.badge).toBe("DoP：処理をかけられません");
    expect(m.output.lines).toContain("DoP（PCM 176.4 kHz の枠）");
  });
  it("lossy files are decoded to floating point; ASIO and the device's own volume", () => {
    const s: PlaybackStatus = {
      ...direct,
      sourceFormat: f(44100, 32, 32, "float"),
      path: path({ container: "MP3" }, { api: "asio", asio: { driverName: "FiiO ASIO Driver", driverVersion: 1552, sampleType: "Int32LSB", rateChanged: false, rateBefore: 44100 }, endpointVolume: null }),
    };
    const m = pathModel(s);
    expect(m.decoder.lines).toEqual(["mpg123", "32bit 浮動小数点"]);
    expect(m.output.lines[0]).toBe("ASIO（FiiO ASIO Driver）");
    const vol = pathModel({ ...direct, path: path({}, { endpointVolume: { scalar: 0.5, muted: false, hardware: true } }) });
    expect(vol.dac.lines).toEqual(["スピーカー (FiiO KA11)", "⚠ 機器の音量 50%"]);
  });
  it("nothing played yet", () => {
    const m = pathModel(emptyStatus);
    expect(m.processing.badge).toBe("—");
    expect(m.file.lines).toEqual(["—"]);
  });
});

describe("BIT-PERFECT and why", () => {
  it("the direct path: every condition met, how far it is checked", () => {
    const v = verdictModel(direct)!;
    expect(v.title).toBe("BIT-PERFECT");
    expect(v.bitPerfect).toBe(true);
    expect(v.checks.map((c) => [c.label, c.state])).toEqual([
      ["音量 100%", "ok"],
      ["処理なし", "ok"],
      ["形式がファイルと一致", "ok"],
      ["排他モード", "ok"],
      ["Windows の音量", "ok"],
      ["自己確認", "ok"],
    ]);
    expect(v.checks[2].detail).toBe("32bit コンテナに詰め替え（値は同じ）");
    expect(v.checks[5].detail).toBe("1 分 5 秒分、ドライバーに渡した値がファイルの値と一致");
    expect(v.scope).toContain("アプリからドライバーまで");
  });
  it("the processing path: what breaks it", () => {
    const v = verdictModel(processed)!;
    expect(v.title).toBe("BIT-PERFECT ではありません");
    const state = (label: string) => v.checks.find((c) => c.label === label)!;
    expect(state("音量 100%")).toMatchObject({ state: "ng", detail: "DIGITAL VOL −6.0 dB" });
    expect(state("処理なし")).toMatchObject({ state: "ng", detail: "パラメトリック EQ、デジタル音量、ISP セーフ、TPDF ディザ" });
    expect(state("形式がファイルと一致")).toMatchObject({ state: "ng" });
    expect(state("自己確認")).toMatchObject({ state: "info" });
  });
  it("the device's own volume below 100%: a warning after the driver", () => {
    const s = { ...direct, path: path({}, { endpointVolume: { scalar: 0.5, muted: false, hardware: true } }) };
    const v = verdictModel({ ...s, bitPerfect: { ...verdict, deviceVolume: 0.5 } })!;
    expect(v.checks.find((c) => c.label === "Windows の音量")).toMatchObject({ state: "warn" });
  });
  it("a mismatch found by the self-check", () => {
    const v = verdictModel({ ...direct, delivery: { matchedFrames: 100, mismatchedFrames: 7 }, bitPerfect: { ...verdict, bitPerfect: false, deliveryMismatch: true } })!;
    expect(v.checks.find((c) => c.label === "自己確認")).toMatchObject({ state: "ng" });
  });
  it("nothing to judge before the output opens", () => {
    expect(verdictModel(emptyStatus)).toBeNull();
  });
});

describe("ISP warning (原音モード)", () => {
  it("on the direct path, a track above 0 dBTP warns", () => {
    const t = ispWarning({ ...direct, trackTruePeakDb: 2.96 });
    expect(t).toContain("+3.0 dBTP");
    expect(t).toContain("0 dBTP を超えています");
  });
  it("not below 0 dBTP, not measured, on the processing path, or for DoP", () => {
    expect(ispWarning({ ...direct, trackTruePeakDb: -0.1 })).toBeNull();
    expect(ispWarning({ ...direct, trackTruePeakDb: null })).toBeNull();
    expect(ispWarning({ ...processed, trackTruePeakDb: 2.96 })).toBeNull();
    expect(ispWarning({ ...direct, trackTruePeakDb: 2.96, dop: { dsdRate: 2822400, container: "DSF" } })).toBeNull();
  });
});

describe("meters", () => {
  it("bars from -60 to 0, overs marked", () => {
    expect(barPercent(-60)).toBe(0);
    expect(barPercent(-30)).toBe(50);
    expect(barPercent(0)).toBe(100);
    expect(barPercent(1.5)).toBe(100);
    expect(barPercent("-inf")).toBe(0);
    expect(barPercent(undefined)).toBe(0);
  });
  it("true peak per channel now and the track's maximum; loudness", () => {
    const s: PlaybackStatus = {
      ...direct,
      meter: {
        state: "measuring",
        queueIndex: 0,
        droppedFrames: 0,
        reading: {
          channels: 2,
          truePeakRecent: [-3.14, 0.4],
          truePeakMax: [-1.2, 0.4],
          vu: [-16, -15],
          momentaryLufs: -14.2,
          shortTermLufs: -14.9,
          integratedLufs: -15.04,
          trackFrames: 96000 * 30,
        },
      },
    };
    const m = meterModel(s);
    expect(m.peaks.map((p) => [p.label, p.now, p.max, p.over])).toEqual([
      ["L", "−3.1", "−1.2", false],
      ["R", "+0.4", "+0.4", true],
    ]);
    expect(m.loudness.momentary).toBe("−14.2");
    expect(m.loudness.shortTerm).toBe("−14.9");
    expect(m.loudness.integrated).toBe("−15.0");
    expect(m.loudness.percent).toBeCloseTo(((60 - 14.2) / 60) * 100);
  });
  it("idle and DoP", () => {
    expect(meterModel(emptyStatus).peaks.map((p) => p.now)).toEqual(["—", "—"]);
    const dop = meterModel({ ...direct, meter: { state: "notPcm", queueIndex: 0, droppedFrames: 0, reading: null } });
    expect(dop.note).toContain("DoP");
  });
});
