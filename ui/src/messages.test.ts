import { describe, expect, it } from "vitest";
import { describeFormat, errorText, formatTime, repackText, stateLabel } from "./messages";
import { emptyStatus, type FormatInfo, type PlaybackStatus } from "./status";

const f = (rate: number, container: number, valid: number, type: "int" | "float" = "int"): FormatInfo => ({
  sampleRate: rate,
  channels: 2,
  containerBits: container,
  validBits: valid,
  sampleType: type,
});

describe("describeFormat", () => {
  it("shows rate, bits and channels", () => {
    expect(describeFormat(f(96000, 24, 24))).toBe("96 kHz / 24bit / 2ch");
    expect(describeFormat(f(44100, 16, 16))).toBe("44.1 kHz / 16bit / 2ch");
  });
  it("mentions a larger container", () => {
    expect(describeFormat(f(352800, 32, 24))).toBe("352.8 kHz / 24bit（32bit コンテナ）/ 2ch");
  });
  it("names float formats", () => {
    expect(describeFormat(f(48000, 32, 32, "float"))).toBe("48 kHz / 32bit 浮動小数点 / 2ch");
  });
});

describe("formatTime", () => {
  it("formats minutes and seconds", () => {
    expect(formatTime(0, 44100)).toBe("0:00");
    expect(formatTime(44100 * 61.9, 44100)).toBe("1:01");
  });
  it("adds hours when needed", () => {
    expect(formatTime(48000 * 3725, 48000)).toBe("1:02:05");
  });
  it("is safe with an unknown rate", () => {
    expect(formatTime(1000, 0)).toBe("0:00");
  });
});

describe("stateLabel", () => {
  it("has a Japanese label for every state", () => {
    for (const s of ["empty", "stopped", "playing", "ended", "error"] as const) {
      expect(stateLabel(s)).not.toBe("");
    }
  });
});

describe("repackText", () => {
  const base: PlaybackStatus = { ...emptyStatus, sourceFormat: f(96000, 24, 24), outputFormat: f(96000, 32, 24) };
  it("says nothing changed when bytes go as read", () => {
    expect(repackText({ ...base, repack: "none" })).toContain("そのまま");
  });
  it("explains a wider container without claiming a change in sound", () => {
    const t = repackText({ ...base, repack: "widerContainer" });
    expect(t).toContain("32bit コンテナ");
    expect(t).toContain("値は変わりません");
  });
});

describe("errorText", () => {
  const withError = (e: Partial<PlaybackStatus["error"]>): PlaybackStatus => ({
    ...emptyStatus,
    state: "error",
    sourceFormat: f(44100, 24, 24),
    error: { ...emptyStatus.error, ...e },
  });

  it("is empty without an error", () => {
    expect(errorText(emptyStatus)).toBe("");
  });
  it("explains an unsupported rate and how to fix it", () => {
    const t = errorText(withError({ kind: "formatNotSupported", reason: "sampleRate" }));
    expect(t).toContain("44.1 kHz");
    expect(t).toContain("変換はしません");
    expect(t).toContain("デバイスの設定");
  });
  it("explains a device held by another application", () => {
    expect(errorText(withError({ kind: "output", outputError: "deviceInUse" }))).toContain("他のアプリ");
  });
  it("explains disabled exclusive mode", () => {
    expect(errorText(withError({ kind: "output", outputError: "exclusiveModeNotAllowed" }))).toContain("排他的に制御");
  });
});
