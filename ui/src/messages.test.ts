import { describe, expect, it } from "vitest";
import {
  describeFormat,
  dropoutText,
  errorText,
  formatTime,
  priorityText,
  queueText,
  repackText,
  stateLabel,
  transitionText,
} from "./messages";
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

describe("queueText", () => {
  it("shows the position in the queue only for more than one track", () => {
    expect(queueText({ ...emptyStatus, state: "playing", queueIndex: 1, queueLength: 3 })).toBe("2 / 3 曲目");
    expect(queueText({ ...emptyStatus, state: "playing", queueIndex: 0, queueLength: 1 })).toBe("");
  });
});

describe("transitionText", () => {
  it("describes a gapless join", () => {
    const t = transitionText({
      fromIndex: 0,
      toIndex: 1,
      kind: "gapless",
      fromFormat: f(44100, 24, 24),
      toFormat: f(44100, 24, 24),
      switchMilliseconds: 0,
    });
    expect(t).toContain("1 → 2 曲目");
    expect(t).toContain("ギャップレス");
    expect(t).not.toContain("ms");
  });
  it("describes a format switch with the time it took", () => {
    const t = transitionText({
      fromIndex: 1,
      toIndex: 2,
      kind: "reopened",
      fromFormat: f(44100, 24, 24),
      toFormat: f(96000, 24, 24),
      switchMilliseconds: 41.6,
    });
    expect(t).toContain("44.1 kHz");
    expect(t).toContain("96 kHz");
    expect(t).toContain("約 42 ms");
  });
});

describe("dropoutText", () => {
  const none = { lateBuffers: 0, deviceTimeouts: 0, readStarvations: 0, starvedFrames: 0, realtimePriority: true };
  it("says 0 when nothing dropped out", () => {
    expect(dropoutText(none)).toBe("0 回");
  });
  it("splits the count by cause", () => {
    const t = dropoutText({ ...none, lateBuffers: 2, readStarvations: 1, starvedFrames: 4800 });
    expect(t).toContain("3 回");
    expect(t).toContain("デバイスへの受け渡しの遅れ 2");
    expect(t).toContain("ファイルの読み込みの遅れ 1");
  });
  it("mentions device timeouts", () => {
    expect(dropoutText({ ...none, deviceTimeouts: 1 })).toContain("デバイスの応答なし 1");
  });
});

describe("priorityText", () => {
  it("names MMCSS without claiming better sound", () => {
    const t = priorityText(true);
    expect(t).toContain("MMCSS");
    expect(t).toContain("音切れ");
    expect(t).not.toContain("音質");
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
  it("names the rates the device accepts now", () => {
    const one = errorText(withError({ kind: "formatNotSupported", reason: "sampleRate", acceptedRates: [96000] }));
    expect(one).toContain("今受け付けるのは 96 kHz です");
    const two = errorText(withError({ kind: "formatNotSupported", reason: "sampleRate", acceptedRates: [44100, 48000] }));
    expect(two).toContain("44.1 kHz、48 kHz");
  });
  it("says so when the device accepts no rate at this channel count", () => {
    const t = errorText(withError({ kind: "formatNotSupported", reason: "sampleRate", acceptedRates: [] }));
    expect(t).not.toContain("今受け付けるのは");
  });
  it("explains a device held by another application", () => {
    expect(errorText(withError({ kind: "output", outputError: "deviceInUse" }))).toContain("他のアプリ");
  });
  it("explains disabled exclusive mode", () => {
    expect(errorText(withError({ kind: "output", outputError: "exclusiveModeNotAllowed" }))).toContain("排他的に制御");
  });
});
