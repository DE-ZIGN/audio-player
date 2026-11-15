import { describe, expect, it } from "vitest";
import {
  activeBands,
  curvePath,
  curveRange,
  dbText,
  defaultListening,
  frequencyText,
  lowCutText,
  modeSummary,
  parseSpaceDetail,
  playingSpaceSource,
  spaceIrText,
  newBand,
  outputLabel,
  outputOptions,
  parseField,
  selectedOutputValue,
  xOf,
  yOf,
  type ListeningSettings,
  type OutputsInfo,
} from "./settings";

const info: OutputsInfo = {
  wasapi: [
    { id: "{a}", name: "スピーカー (Realtek)", isDefault: true },
    { id: "{b}", name: "FiiO KA11" },
  ],
  asio: [{ id: "{C1}", name: "FiiO ASIO Driver" }],
};

describe("mode summary", () => {
  it("原音モード says nothing is processed and that ReplayGain and the EQ are kept but not applied", () => {
    const t = modeSummary({ ...defaultListening, replayGain: "track" });
    expect(t).toContain("原音モード");
    expect(t).toContain("何も処理しません");
    expect(t).toContain("ReplayGain・EQ・トーン・真空管・空間・クロスフィードはかけません");
    expect(t).not.toContain("ISP");
  });
  it("原音モード with the ISP-safe gain on says so", () => {
    expect(modeSummary({ ...defaultListening, ispSafeOriginal: "fixed" })).toContain("ISP セーフ（固定で −3 dB）");
  });
  it("音作りモード lists what it applies", () => {
    const l: ListeningSettings = {
      ...defaultListening,
      mode: "processed",
      replayGain: "album",
      eqBands: [
        { type: "peak", frequency: 1000, gainDb: 3, q: 1, enabled: true },
        { type: "peak", frequency: 2000, gainDb: 0, q: 1, enabled: true }, // flat: not counted
        { type: "highPass", frequency: 20, gainDb: 0, q: 0.7, enabled: true }, // a pass filter counts
        { type: "peak", frequency: 3000, gainDb: 3, q: 1, enabled: false },
      ],
    };
    expect(modeSummary(l)).toBe(
      "音作りモード：ReplayGain（アルバム）、EQ（2 バンド）、ISP セーフ（計測した値で下げる）を 64bit 浮動小数点でかけます。",
    );
  });
  it("音作りモード lists the tone and the tube (task 9-3)", () => {
    const l: ListeningSettings = {
      ...defaultListening,
      mode: "processed",
      ispSafeProcessed: "off",
      toneBassDb: 3,
      toneTrebleDb: 0,
      tubeEnabled: true,
      tubeType: "ppel34",
      tubeDriveDb: -6,
    };
    expect(modeSummary(l)).toBe("音作りモード：トーン（BASS +3.0 dB）、真空管（PP EL34、DRIVE -6.0 dB）を 64bit 浮動小数点でかけます。");
    expect(modeSummary({ ...l, crossfeedEnabled: true, crossfeedLevel: "default" })).toContain(
      "真空管（PP EL34、DRIVE -6.0 dB）、クロスフィード（700 Hz・4.5 dB）",
    );
    // Off and flat: not listed.
    expect(modeSummary({ ...l, tubeEnabled: false, toneBassDb: 0 })).toContain("かける処理がありません");
    // The tube type by genre (task 9-7).
    expect(modeSummary(l, true)).toContain("真空管（ジャンルで自動・対応表にないときは PP EL34、DRIVE -6.0 dB）");
  });
  it("音作りモード with nothing to apply says the output is the same as 原音モード", () => {
    const t = modeSummary({ ...defaultListening, mode: "processed", ispSafeProcessed: "off" });
    expect(t).toContain("かける処理がありません");
  });
  it("the EQ switched off counts no bands", () => {
    const l = { ...defaultListening, eqEnabled: false, eqBands: [{ type: "peak" as const, frequency: 1000, gainDb: 3, q: 1, enabled: true }] };
    expect(activeBands(l)).toBe(0);
  });
});

describe("numbers", () => {
  it("dB with a sign, and frequencies", () => {
    expect(dbText(-3.14)).toBe("−3.1 dB");
    expect(dbText(2)).toBe("+2.0 dB");
    expect(dbText(-0.04)).toBe("0.0 dB");
    expect(frequencyText(105)).toBe("105 Hz");
    expect(frequencyText(31.5)).toBe("31.5 Hz");
    expect(frequencyText(16000)).toBe("16 kHz");
    expect(frequencyText(1250)).toBe("1.25 kHz");
  });
  it("typed fields: limited, full-width and minus signs accepted, junk refused", () => {
    expect(parseField("gainDb", "−3,5")).toBe(-3.5);
    expect(parseField("gainDb", "40")).toBe(24);
    expect(parseField("frequency", "5")).toBe(10);
    expect(parseField("q", "0")).toBe(0.1);
    expect(parseField("q", "abc")).toBeNull();
    expect(parseField("q", " ")).toBeNull();
  });
  it("a new band is flat, at a frequency not used yet", () => {
    expect(newBand([])).toEqual({ type: "peak", frequency: 1000, gainDb: 0, q: 1, enabled: true });
    expect(newBand([newBand([])]).frequency).toBe(250);
  });
});

describe("output list", () => {
  it("the Windows default first (naming the device it is now), then WASAPI devices, then ASIO drivers", () => {
    const o = outputOptions(info, { kind: "wasapi", id: "", name: "", allowDop: false });
    expect(o.map((x) => x.value)).toEqual(["wasapi:", "wasapi:{a}", "wasapi:{b}", "asio:{C1}"]);
    expect(o[0].label).toBe("Windows の既定のデバイス（今は スピーカー (Realtek)）");
    expect(o[3].group).toBe("asio");
  });
  it("a chosen device that is not connected stays in the list, marked", () => {
    const gone = { kind: "asio" as const, id: "{GONE}", name: "Old ASIO", allowDop: true };
    const o = outputOptions(info, gone);
    expect(o.at(-1)).toMatchObject({ value: "asio:{GONE}", label: "Old ASIO（今は見つかりません）", missing: true });
    expect(selectedOutputValue(info, gone)).toBe("asio:{GONE}");
  });
  it("a WASAPI device whose ID changed is found by its name", () => {
    const moved = { kind: "wasapi" as const, id: "{old}", name: "FiiO KA11", allowDop: false };
    expect(outputOptions(info, moved).some((x) => x.missing)).toBe(false);
    expect(selectedOutputValue(info, moved)).toBe("wasapi:{b}");
  });
  it("labels", () => {
    expect(outputLabel({ kind: "wasapi", id: "", name: "", allowDop: false })).toBe("WASAPI 排他：Windows の既定のデバイス");
    expect(outputLabel({ kind: "asio", id: "{C1}", name: "FiiO ASIO Driver", allowDop: false })).toBe("ASIO：FiiO ASIO Driver");
  });
});

describe("curve", () => {
  const frame = { width: 400, height: 100, minHz: 20, maxHz: 20000, rangeDb: 12 };
  it("log frequency axis and a symmetric dB axis", () => {
    expect(xOf(20, frame)).toBe(0);
    expect(xOf(20000, frame)).toBeCloseTo(400);
    expect(xOf(Math.sqrt(20 * 20000), frame)).toBeCloseTo(200);
    expect(yOf(0, frame)).toBe(50);
    expect(yOf(12, frame)).toBe(0);
    expect(yOf(-30, frame)).toBe(100); // clipped
  });
  it("path data, with an offset (the preamp)", () => {
    expect(curvePath([[20, 0], [20000, 6]], frame)).toBe("M0.0 50.0 L400.0 25.0");
    expect(curvePath([[20, 0]], frame, -6)).toBe("M0.0 75.0");
  });
  it("range: the smallest of ±6, ±12, ±24 that shows the curve", () => {
    expect(curveRange([[20, 3]])).toBe(6);
    expect(curveRange([[20, 3]], -9)).toBe(12);
    expect(curveRange([[20, -18]])).toBe(24);
  });
});

describe("the space (task 9-6)", () => {
  it("is in the mode summary with its room and MIX, between the tube and the crossfeed; not at MIX 0", () => {
    const l: ListeningSettings = {
      ...defaultListening,
      mode: "processed",
      ispSafeProcessed: "off",
      tubeEnabled: true,
      crossfeedEnabled: true,
      spaceRoom: "hall",
      spaceMixPercent: 30,
    };
    // HALL plays its measured IR by default.
    expect(modeSummary(l)).toContain("真空管（SE 300B、DRIVE -12.0 dB）、空間（HALL（実測）・MIX 30%）、クロスフィード");
    expect(modeSummary({ ...l, spaceSource: ["synth", "synth", "synth"] })).toContain("空間（HALL・MIX 30%）");
    expect(modeSummary({ ...l, spaceSource: ["synth", "file", "synth"], spaceIr: ["", "C:\\ir.wav", ""] })).toContain(
      "空間（HALL（自分の IR）・MIX 30%）",
    );
    // A file chosen as the source but none given: the room's default.
    expect(modeSummary({ ...l, spaceSource: ["synth", "file", "synth"] })).toContain("空間（HALL（実測）・MIX 30%）");
    expect(modeSummary({ ...l, spaceRoom: "club" })).toContain("空間（CLUB・MIX 30%）");
    expect(modeSummary({ ...l, spaceMixPercent: 0 })).not.toContain("空間");
    expect(modeSummary({ ...l, spaceRoom: "off" })).not.toContain("空間");
  });
  it("reads the stage's detail", () => {
    expect(parseSpaceDetail("club;20")).toEqual({ room: "club", mix: 20, source: "synth" });
    expect(parseSpaceDetail("hall;30;measured")).toEqual({ room: "hall", mix: 30, source: "measured" });
    expect(parseSpaceDetail("arena;5;user")).toEqual({ room: "arena", mix: 5, source: "user" });
    expect(parseSpaceDetail("off;0")).toBeNull();
    expect(parseSpaceDetail(undefined)).toBeNull();
  });
  it("says what each room's IR is: measured or synthesized with its numbers, the user's file, or why not", () => {
    const d = defaultListening;
    expect(spaceIrText(d, "hall", undefined)).toBe("実測：St Paul's Hall（英国、ハダースフィールド大学のコンサートホール）・残響 2.1 秒");
    expect(spaceIrText(d, "club", undefined)).toBe("合成：9 × 14 × 3.5 m・残響 0.75 秒");
    expect(spaceIrText({ ...d, spaceSource: ["synth", "synth", "synth"] }, "hall", undefined)).toBe("合成：25 × 40 × 16 m・残響 2.0 秒");
    const base = { loaded: true, seconds: 2.5, sampleRate: 48000, channels: 2, fileChannels: 2, truncated: false };
    const club = (path: string) => ({ ...d, spaceSource: ["file", "measured", "synth"], spaceIr: [path, "", ""] }) as ListeningSettings;
    expect(spaceIrText(club("C:\\IR\\room.wav"), "club", { ...base, path: "C:\\IR\\room.wav" })).toBe("room.wav（2.50 秒・ステレオ・48.0 kHz）");
    expect(
      spaceIrText(club("D:/ir/long.flac"), "club", { ...base, path: "D:/ir/long.flac", fileChannels: 4, truncated: true }),
    ).toBe("long.flac（2.50 秒・ステレオ・48.0 kHz・4ch のうち最初の 2ch・5 秒で切りました）");
    expect(spaceIrText(club("x.wav"), "club", { ...base, path: "x.wav", loaded: false, error: "音が入っていません" })).toBe(
      "⚠ x.wav を読めませんでした（音が入っていません）。内蔵（合成）を使っています",
    );
    const hallFile = { ...d, spaceSource: ["synth", "file", "synth"], spaceIr: ["", "y.wav", ""] } as ListeningSettings;
    expect(spaceIrText(hallFile, "hall", { ...base, path: "y.wav", loaded: false, error: "見つかりません" })).toBe(
      "⚠ y.wav を読めませんでした（見つかりません）。内蔵（実測）を使っています",
    );
    expect(spaceIrText({ ...hallFile, spaceIr: ["", "", ""] }, "hall", undefined)).toBe(
      "自分のファイルはまだ選んでいません。内蔵（実測）を使っています",
    );
    // A file kept while another source is chosen is not what plays.
    expect(spaceIrText({ ...hallFile, spaceSource: ["synth", "measured", "synth"] }, "hall", { ...base, path: "y.wav" })).toContain("実測");
    expect(playingSpaceSource(hallFile, "hall", { ...base, path: "y.wav" })).toBe("user");
    expect(playingSpaceSource({ ...d, spaceSource: ["measured", "measured", "measured"] }, "club")).toBe("synth");
    expect(lowCutText(0)).toBe("なし");
    expect(lowCutText(120)).toBe("120 Hz");
  });
});
