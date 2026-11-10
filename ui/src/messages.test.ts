import { describe, expect, it } from "vitest";
import {
  bitPerfectText,
  describeFormat,
  dopNotice,
  dsdRateName,
  inputText,
  processingText,
  dropoutText,
  errorText,
  formatTime,
  integrityFailuresText,
  integrityText,
  outputModeText,
  priorityText,
  queueText,
  rateChangeText,
  repackText,
  stageText,
  stateLabel,
  transitionText,
  volumeText,
} from "./messages";
import {
  emptyStatus,
  type BitPerfectVerdict,
  type FormatInfo,
  type PlaybackStatus,
  type SignalPath,
  type StageState,
} from "./status";

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

describe("DoP texts", () => {
  const dopStatus: PlaybackStatus = {
    ...emptyStatus,
    state: "playing",
    sourceFormat: { sampleRate: 176400, channels: 2, containerBits: 24, validBits: 24, sampleType: "int" },
    dop: { dsdRate: 2822400, container: "DSF" },
  };
  it("names DSD rates", () => {
    expect(dsdRateName(2822400)).toBe("DSD64");
    expect(dsdRateName(5644800)).toBe("DSD128");
    expect(dsdRateName(11289600)).toBe("DSD256");
    expect(dsdRateName(3072000)).toBe("DSD64（48 kHz 系）");
  });
  it("describes the input as DSD, not as the PCM wrapper", () => {
    const t = inputText(dopStatus);
    expect(t).toContain("DSD64");
    expect(t).toContain("2.8224 MHz");
    expect(t).toContain("DSF");
    expect(inputText({ ...dopStatus, dop: null })).toBe("176.4 kHz / 24bit / 2ch");
  });
  it("says that DoP cannot be processed", () => {
    expect(processingText(dopStatus)).toContain("DoP の経路では処理をかけられません");
    expect(processingText({ ...dopStatus, dop: null })).toContain("原音経路");
  });
  it("describes the processing path: stages, quantization of float tracks, dither", () => {
    const base = { ...dopStatus, dop: null };
    const path = (p: Partial<SignalPath>): SignalPath => ({
      container: "MP3",
      processing: true,
      processingStages: [],
      processingRequired: false,
      dither: true,
      comparison: false,
      output: {
        api: "wasapiExclusive",
        periodFrames: 480,
        periodMilliseconds: 10,
        eventDriven: true,
        layout: "interleaved",
        asio: null,
        endpointVolume: null,
      },
      ...p,
    });
    const quantized = processingText({ ...base, path: path({ processingRequired: true }) });
    expect(quantized).toContain("浮動小数点の曲を整数にする");
    expect(quantized).toContain("TPDF ディザ");
    expect(quantized).not.toContain("原音経路");
    const volume = processingText({ ...base, path: path({ processingStages: ["volume"] }) });
    expect(volume).toContain("デジタル音量");
    expect(processingText({ ...base, path: path({ dither: false }) })).not.toContain("ディザ");
  });
  it("warns about DACs without DoP", () => {
    const t = dopNotice(dopStatus);
    expect(t).toContain("176.4 kHz");
    expect(t).toContain("DoP に対応していない DAC");
    expect(dopNotice({ ...dopStatus, dop: null })).toBe("");
  });
});

describe("output path texts", () => {
  const wasapi: SignalPath = {
    container: "WAV",
    processing: false,
    processingStages: [],
    processingRequired: false,
    dither: false,
    comparison: false,
    output: {
      api: "wasapiExclusive",
      periodFrames: 960,
      periodMilliseconds: 10,
      eventDriven: true,
      layout: "interleaved",
      asio: null,
      endpointVolume: { scalar: 1, muted: false, hardware: false },
    },
  };
  const asio = (layout: SignalPath["output"]["layout"], sampleType: string, rateChanged = false): SignalPath => ({
    ...wasapi,
    output: {
      ...wasapi.output,
      api: "asio",
      periodFrames: 512,
      periodMilliseconds: 10.67,
      eventDriven: false,
      layout,
      asio: { driverName: "SSL ASIO Driver", driverVersion: 3, sampleType, rateChanged, rateBefore: 96000 },
      endpointVolume: null,
    },
  });
  const withPath = (path: SignalPath | null): PlaybackStatus => ({
    ...emptyStatus,
    state: "playing",
    outputFormat: f(44100, 24, 24),
    path,
  });

  it("names exclusive WASAPI and the period", () => {
    const t = outputModeText(wasapi);
    expect(t).toContain("排他 WASAPI");
    expect(t).toContain("イベント駆動");
    expect(t).toContain("960 フレーム");
    expect(t).toContain("10 ms");
  });
  it("names the ASIO driver and its sample type", () => {
    const t = outputModeText(asio("perChannel", "Int24LSB"));
    expect(t).toContain("ASIO");
    expect(t).toContain("SSL ASIO Driver");
    expect(t).toContain("Int24LSB");
    expect(t).toContain("512 フレーム");
    expect(t).not.toContain("右詰め");
  });
  it("explains a right-aligned ASIO sample type without claiming a change", () => {
    const t = outputModeText(asio("perChannelRightAligned", "Int32LSB24"));
    expect(t).toContain("Int32LSB24");
    expect(t).toContain("右詰め");
    expect(t).toContain("値は変わりません");
  });
  it("says when the driver's rate was switched", () => {
    expect(rateChangeText(withPath(asio("perChannel", "Int32LSB", true)))).toContain("96 kHz → 44.1 kHz");
    expect(rateChangeText(withPath(asio("perChannel", "Int32LSB", false)))).toBe("");
    expect(rateChangeText(withPath(wasapi))).toBe("");
  });
  it("explains the Windows volume depending on where it is applied", () => {
    const software = volumeText({ ...wasapi, output: { ...wasapi.output, endpointVolume: { scalar: 0.9, muted: false, hardware: false } } });
    expect(software).toContain("90%");
    expect(software).toContain("排他モードではかかりません");

    const hardware = volumeText({ ...wasapi, output: { ...wasapi.output, endpointVolume: { scalar: 0.5, muted: false, hardware: true } } });
    expect(hardware).toContain("50%");
    expect(hardware).toContain("デバイスの中");
    expect(hardware).toContain("100%");

    const full = volumeText({ ...wasapi, output: { ...wasapi.output, endpointVolume: { scalar: 1, muted: false, hardware: true } } });
    expect(full).toContain("100%");
    expect(full).not.toContain("にしてください");

    const muted = volumeText({ ...wasapi, output: { ...wasapi.output, endpointVolume: { scalar: 1, muted: true, hardware: true } } });
    expect(muted).toContain("ミュート");
  });
  it("shows nothing about volume when the output has none", () => {
    expect(volumeText(asio("perChannel", "Int24LSB"))).toBe("");
  });
});

describe("integrity texts", () => {
  const withCheck = (state: PlaybackStatus["integrity"]["state"], decodeErrors = 0, failures: number[] = []): PlaybackStatus => ({
    ...emptyStatus,
    state: "playing",
    integrity: { state, decodeErrors },
    integrityFailures: failures,
  });
  it("explains each state of the MD5 check", () => {
    expect(integrityText(withCheck("passed"))).toContain("一致");
    expect(integrityText(withCheck("pending"))).toContain("最後まで");
    expect(integrityText(withCheck("skipped"))).toContain("シーク");
    expect(integrityText(withCheck("notAvailable"))).toContain("記録されていません");
  });
  it("says what a failure means without guessing more", () => {
    const t = integrityText(withCheck("failed"));
    expect(t).toContain("一致しません");
    expect(t).toContain("壊れている可能性");
  });
  it("mentions decoder errors", () => {
    expect(integrityText(withCheck("failed", 3))).toContain("3 か所");
  });
  it("names the file type in the input line once it is known", () => {
    const s: PlaybackStatus = {
      ...emptyStatus,
      sourceFormat: f(96000, 24, 20),
      path: {
        container: "FLAC",
        processing: false,
        processingStages: [],
        processingRequired: false,
        dither: false,
        comparison: false,
        output: {
          api: "dummy",
          periodFrames: 480,
          periodMilliseconds: 5,
          eventDriven: false,
          layout: "interleaved",
          asio: null,
          endpointVolume: null,
        },
      },
    };
    expect(inputText(s)).toBe("96 kHz / 20bit（24bit コンテナ）/ 2ch（FLAC）");
  });
  it("lists earlier tracks whose check failed", () => {
    expect(integrityFailuresText(withCheck("passed", 0, [0, 2]))).toContain("1、3 曲目");
    expect(integrityFailuresText(withCheck("passed"))).toBe("");
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
  // Float sources on integer-only devices are quantized with dither (task 3-2) and do not
  // reach this error; an integer source on a float-only device is refused, not converted.
  it("explains why an integer source does not play on a float-only device, without converting", () => {
    const t = errorText({ ...withError({ kind: "formatNotSupported", reason: "sampleType" }), sourceFormat: f(44100, 24, 24) });
    expect(t).toContain("浮動小数点だけ");
    expect(t).toContain("変換しない");
  });
  it("explains a device held by another application", () => {
    expect(errorText(withError({ kind: "output", outputError: "deviceInUse" }))).toContain("他のアプリ");
  });
  it("explains that DoP is off and why", () => {
    const t = errorText(withError({ kind: "dopNotAllowed" }));
    expect(t).toContain("DoP");
    expect(t).toContain("ノイズ");
  });
  it("explains disabled exclusive mode", () => {
    expect(errorText(withError({ kind: "output", outputError: "exclusiveModeNotAllowed" }))).toContain("排他的に制御");
  });
});

describe("BIT-PERFECT (task 4-2)", () => {
  const verdict = (v: Partial<BitPerfectVerdict>): BitPerfectVerdict => ({
    bitPerfect: true,
    quantized: false,
    dither: false,
    comparison: false,
    repacked: false,
    dop: false,
    floatSource: false,
    dummyOutput: false,
    deviceVolume: null,
    deviceMuted: false,
    ...v,
  });
  const withVerdict = (v: Partial<BitPerfectVerdict>, stages: StageState[] = []): PlaybackStatus => ({
    ...emptyStatus,
    bitPerfect: verdict(v),
    stages,
  });

  it("shows nothing before the output is open", () => {
    expect(bitPerfectText(emptyStatus)).toBe("—");
  });
  it("says BIT-PERFECT on the direct path, up to the driver", () => {
    const t = bitPerfectText(withVerdict({}));
    expect(t.startsWith("BIT-PERFECT（")).toBe(true);
    expect(t).toContain("ドライバーまで");
  });
  it("keeps BIT-PERFECT for a container repack and DoP, with a note", () => {
    expect(bitPerfectText(withVerdict({ repacked: true }))).toContain("コンテナの詰め替えあり、値は同じ");
    expect(bitPerfectText(withVerdict({ dop: true }))).toContain("DoP");
    expect(bitPerfectText(withVerdict({ floatSource: true }))).toContain("デコードした値のまま");
  });
  it("notes the device's own volume as after the driver", () => {
    const t = bitPerfectText(withVerdict({ deviceVolume: 0.37 }));
    expect(t.startsWith("BIT-PERFECT")).toBe(true);
    expect(t).toContain("デバイス側の音量 37%");
    expect(bitPerfectText(withVerdict({ deviceVolume: 0.5, deviceMuted: true }))).toContain("ミュート");
  });
  it("is not BIT-PERFECT with the digital volume, and says DIGITAL VOL with the value", () => {
    const t = bitPerfectText(
      withVerdict({ bitPerfect: false, dither: true }, [
        { name: "volume", gainDb: -6 },
        { name: "limiter", gainDb: null },
      ]),
    );
    expect(t.startsWith("BIT-PERFECT ではありません")).toBe(true);
    expect(t).toContain("DIGITAL VOL -6.0 dB");
    expect(t).not.toContain("リミッター"); // the safety limiter is not a reason by itself
    expect(t).toContain("TPDF ディザ");
  });
  it("lists ReplayGain, the EQ's preamp and ISP-safe with their values", () => {
    const t = bitPerfectText(
      withVerdict({ bitPerfect: false, dither: true }, [
        { name: "replaygain", gainDb: -5.56 },
        { name: "eq", gainDb: -5.49 },
        { name: "isp-safe", gainDb: -3.46 },
      ]),
    );
    expect(t).toContain("ReplayGain -5.6 dB");
    expect(t).toContain("パラメトリック EQ（プリアンプ -5.5 dB）");
    expect(t).toContain("ISP セーフ -3.5 dB");
  });
  it("names a float source made integer, the comparison, and mute", () => {
    expect(bitPerfectText(withVerdict({ bitPerfect: false, quantized: true, dither: true }))).toContain(
      "浮動小数点の曲を整数に変換",
    );
    expect(bitPerfectText(withVerdict({ bitPerfect: false, comparison: true }, [{ name: "comparison", gainDb: null }]))).toContain(
      "聴き比べ中",
    );
    expect(stageText({ name: "volume", gainDb: "-inf" })).toBe("デジタル音量 -∞ dB");
  });
  it("shows the processing stages with their current values", () => {
    const s: PlaybackStatus = {
      ...emptyStatus,
      path: {
        container: "FLAC",
        processing: true,
        processingStages: ["volume"],
        processingRequired: false,
        dither: true,
        comparison: false,
        output: {
          api: "wasapiExclusive",
          periodFrames: 480,
          periodMilliseconds: 10,
          eventDriven: true,
          layout: "interleaved",
          asio: null,
          endpointVolume: null,
        },
      },
      stages: [{ name: "volume", gainDb: -12 }],
    };
    expect(processingText(s)).toContain("デジタル音量 -12.0 dB");
  });
  it("describes a switch between the direct and the processing path within a track", () => {
    const t = transitionText({
      fromIndex: 0,
      toIndex: 0,
      kind: "processingChanged",
      fromFormat: f(48000, 16, 16),
      toFormat: f(48000, 16, 16),
      switchMilliseconds: 24.6,
    });
    expect(t).toContain("1 曲目：処理経路と原音経路の切り替え");
    expect(t).toContain("約 25 ms");
  });
});
