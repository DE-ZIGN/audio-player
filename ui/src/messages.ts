// User-facing text (the tables: i18n/parts/status.ts). Describes what the
// player does; never claims that something sounds better.

import { CROSSFEED_LEVELS, TUBE_LABELS, parseSpaceDetail, spaceText, type CrossfeedLevel } from "./settings";
import { characterParts, parseTubeDetail } from "./tubeCharacter";
import { resampleText } from "./outputRate";
import { t, type MessageKey } from "./i18n";
import { nativeText } from "./nativeText";
import type {
  DropoutCounts,
  FormatInfo,
  Level,
  PlaybackStatus,
  PlayerState,
  SignalPath,
  StageState,
  TrackTransition,
} from "./status";

const list = (items: string[]) => items.join(t("list.separator"));

function rateText(rate: number): string {
  return `${Number((rate / 1000).toFixed(3))} kHz`;
}

// The bits of a format: "24bit", "32bit 浮動小数点", "24bit（32bit コンテナ）"
// (padded: the valid bits in a wider container).
export function bitsText(f: FormatInfo): { text: string; padded: boolean } {
  if (f.sampleType === "float") return { text: t("status.format.float", { bits: f.containerBits }), padded: false };
  if (f.validBits === f.containerBits) return { text: `${f.validBits}bit`, padded: false };
  return { text: t("status.format.padded", { valid: f.validBits, container: f.containerBits }), padded: true };
}

export function describeFormat(f: FormatInfo): string {
  const bits = bitsText(f);
  const values = { rate: rateText(f.sampleRate), bits: bits.text, channels: f.channels };
  return bits.padded ? t("status.format.linePadded", values) : t("status.format.line", values);
}

export function formatTime(frames: number, sampleRate: number): string {
  if (!sampleRate || frames <= 0) return "0:00";
  const total = Math.floor(frames / sampleRate);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  const ss = String(s).padStart(2, "0");
  return h > 0 ? `${h}:${String(m).padStart(2, "0")}:${ss}` : `${m}:${ss}`;
}

const STATE_KEYS: Record<PlayerState, MessageKey> = {
  empty: "status.state.empty",
  stopped: "status.state.stopped",
  playing: "status.state.playing",
  ended: "status.state.ended",
  error: "status.state.error",
};

export function stateLabel(state: PlayerState): string {
  return t(STATE_KEYS[state]);
}

export function queueText(s: PlaybackStatus): string {
  return s.queueLength > 1 ? t("status.queue.position", { index: s.queueIndex + 1, count: s.queueLength }) : "";
}

export function transitionText(tr: TrackTransition): string {
  const ms = Math.round(tr.switchMilliseconds);
  if (tr.kind === "gapless") return t("status.transition.gapless", { from: tr.fromIndex + 1, to: tr.toIndex + 1 });
  if (tr.kind === "processingChanged") return t("status.transition.processingChanged", { track: tr.fromIndex + 1, ms });
  return t("status.transition.format", {
    from: tr.fromIndex + 1,
    to: tr.toIndex + 1,
    fromFormat: describeFormat(tr.fromFormat),
    toFormat: describeFormat(tr.toFormat),
    ms,
  });
}

// "DSD64", or "DSD64" of the 48 kHz family (marked in the long name only).
function dsdRate(rate: number): { name: string; family48: boolean } {
  if (rate % 44100 === 0 && (rate / 44100) % 64 === 0) return { name: `DSD${rate / 44100}`, family48: false };
  if (rate % 48000 === 0 && (rate / 48000) % 64 === 0) return { name: `DSD${rate / 48000}`, family48: true };
  return { name: `DSD ${rate} Hz`, family48: false };
}

export function dsdRateName(rate: number): string {
  const d = dsdRate(rate);
  return d.family48 ? t("status.dsd.rate48", { multiple: rate / 48000 }) : d.name;
}

// The "input" line: DSD files are described as DSD, not as their DoP wrapper.
export function inputText(s: PlaybackStatus): string {
  if (!s.sourceFormat) return "—";
  if (!s.dop) {
    const format = describeFormat(s.sourceFormat);
    return s.path?.container ? t("status.input.container", { format, container: s.path.container }) : format;
  }
  const mhz = Number((s.dop.dsdRate / 1_000_000).toFixed(4));
  return t("status.input.dsd", {
    dsd: dsdRateName(s.dop.dsdRate),
    mhz,
    channels: s.sourceFormat.channels,
    container: s.dop.container,
  });
}

const STAGE_KEYS: Record<string, MessageKey> = {
  volume: "status.stage.volume",
  eq: "status.stage.eq",
  tone: "status.stage.tone",
  tube: "status.stage.tube",
  space: "status.stage.space",
  crossfeed: "status.stage.crossfeed",
  "isp-safe": "status.stage.ispSafe",
  limiter: "status.stage.limiter",
  resampler: "status.stage.resampler",
  comparison: "status.stage.comparison",
};

// A stage's name ("デジタル音量"), or its id when it is not one we know.
export function stageName(id: string): string {
  if (id === "replaygain") return "ReplayGain";
  const key = STAGE_KEYS[id];
  return key ? t(key) : id;
}

// The BIT-PERFECT line (CLAUDE.md chapter 8): shown only when the engine
// judged it from the facts of the path; otherwise why not, with the values
// (e.g. "DIGITAL VOL -6.0 dB").
export function bitPerfectText(s: PlaybackStatus): string {
  const v = s.bitPerfect;
  if (!v) return "—";
  const withDevice = (text: string) =>
    v.deviceVolume !== null
      ? t("status.bitPerfect.deviceVolume", {
          text,
          volume: v.deviceMuted ? t("status.bitPerfect.deviceMuted") : `${Math.round(v.deviceVolume * 100)}%`,
        })
      : text;
  if (v.bitPerfect) {
    const notes: string[] = [];
    if (v.dop) notes.push(t("status.bitPerfect.dop"));
    if (v.repacked) notes.push(t("status.bitPerfect.repacked"));
    if (v.floatSource) notes.push(t("status.bitPerfect.floatSource"));
    if (v.dummyOutput) notes.push(t("status.bitPerfect.dummyOutput"));
    return withDevice(notes.length ? t("status.bitPerfect.yesNotes", { notes: list(notes) }) : t("status.bitPerfect.yes"));
  }
  const reasons = s.stages
    .filter((st) => st.name !== "limiter" && st.name !== "comparison")
    .map((st) => (st.name === "volume" && st.gainDb !== null ? `DIGITAL VOL ${dbText(st.gainDb)}` : stageText(st)));
  if (v.quantized) reasons.push(t("status.stage.quantize"));
  if (v.comparison) reasons.push(t("status.bitPerfect.comparison"));
  if (v.dither) reasons.push(t("status.stage.dither"));
  if (v.deliveryMismatch && s.delivery) reasons.push(t("status.bitPerfect.mismatch", { frames: s.delivery.mismatchedFrames }));
  return withDevice(t("status.bitPerfect.no", { reasons: list(reasons) }));
}

function dbText(db: number | "-inf"): string {
  if (db === "-inf") return "-∞ dB";
  return `${db > 0 ? "+" : ""}${db.toFixed(1)} dB`;
}

// One stage with its current value, e.g. "デジタル音量 -6.0 dB".
export function stageText(st: StageState): string {
  const name = stageName(st.name);
  if (st.name === "space") {
    const sp = parseSpaceDetail(st.detail);
    return sp ? t("status.stage.detail", { name, detail: spaceText(sp.room, sp.mix, sp.source) }) : name;
  }
  if (st.name === "crossfeed") {
    const level = CROSSFEED_LEVELS[st.detail as CrossfeedLevel];
    return level ? t("status.stage.detail", { name, detail: level.numbers }) : name;
  }
  if (st.name === "resampler") return t("status.stage.detail", { name, detail: resampleText(st.detail) });
  if (st.gainDb === null) return name;
  if (st.name === "eq" || st.name === "tone") return t("status.stage.preamp", { name, db: dbText(st.gainDb) });
  if (st.name === "tube") {
    const d = parseTubeDetail(st.detail);
    const parts = [
      ...(d.type ? [TUBE_LABELS[d.type]] : []),
      `DRIVE ${dbText(st.gainDb)}`,
      ...characterParts(d.warmUpSeconds, d.unit), // task 9-8
    ];
    return t("status.stage.detail", { name, detail: list(parts) });
  }
  return `${name} ${dbText(st.gainDb)}`;
}

export function processingText(s: PlaybackStatus): string {
  if (s.dop) return t("status.processing.dop");
  const p = s.path;
  if (!p || !p.processing) return t("status.processing.direct");
  const parts = s.stages.length > 0 ? s.stages.map(stageText) : p.processingStages.map(stageName);
  if (p.processingRequired) parts.push(t("status.processing.toInteger"));
  if (p.dither) parts.push(t("status.stage.dither"));
  return t("status.processing.on", { stages: list(parts) });
}

export function dopNotice(s: PlaybackStatus): string {
  if (!s.dop || !s.sourceFormat) return "";
  return t("status.dop.notice", { rate: rateText(s.sourceFormat.sampleRate) });
}

function periodText(frames: number, ms: number): string {
  return t("status.outputMode.period", { frames, ms: Number(ms.toFixed(2)) });
}

// How the output layer delivers the data (from what the output reported).
export function outputModeText(path: SignalPath): string {
  const o = path.output;
  const period = periodText(o.periodFrames, o.periodMilliseconds);
  switch (o.api) {
    case "wasapiExclusive":
      return t(o.eventDriven ? "status.outputMode.wasapiEvent" : "status.outputMode.wasapi", { period });
    case "asio": {
      const a = o.asio;
      return t("status.outputMode.asio", {
        driver: a ? t("status.outputMode.asioDriver", { name: a.driverName, version: a.driverVersion }) : "",
        type: a ? t("status.outputMode.asioType", { type: a.sampleType }) : "",
        aligned: o.layout === "perChannelRightAligned" ? t("status.outputMode.asioRightAligned") : "",
        period,
      });
    }
    case "dummy":
      return t("status.outputMode.dummy", { period });
  }
}

// ASIO: the driver's rate was switched for this track.
export function rateChangeText(s: PlaybackStatus): string {
  const a = s.path?.output.asio;
  if (!a || !a.rateChanged || !s.outputFormat) return "";
  return t("status.rateChange.asio", { from: rateText(a.rateBefore), to: rateText(s.outputFormat.sampleRate) });
}

// Windows endpoint volume (WASAPI). Empty when the output has no volume control.
export function volumeText(path: SignalPath): string {
  const v = path.output.endpointVolume;
  if (!v) return "";
  const percent = Math.round(v.scalar * 100);
  if (!v.hardware) return t(v.muted ? "status.volume.softwareMuted" : "status.volume.software", { percent });
  if (v.muted) return t("status.volume.muted");
  if (percent >= 100) return t("status.volume.max");
  return t("status.volume.lowered", { percent });
}

// "3 分 5 秒", "42 秒".
export function durationText(seconds: number): string {
  const m = Math.floor(seconds / 60);
  const sec = Math.floor(seconds % 60);
  return m > 0 ? t("status.duration.minutes", { minutes: m, seconds: sec }) : t("status.duration.seconds", { seconds: sec });
}

// How much: "3 分 5 秒分" (as time) or "1024 フレーム分".
export function amountText(frames: number, rate: number, asTime: boolean): string {
  return asTime ? t("status.amount.time", { duration: durationText(frames / rate) }) : t("status.amount.frames", { frames });
}

// The self-check of the direct path (task 4-3), with how far it reaches.
export function selfCheckText(s: PlaybackStatus): string {
  if (!s.path) return "—";
  if (s.path.processing) return t("status.selfCheck.processing");
  const d = s.delivery;
  if (!d) return "—";
  if (d.mismatchedFrames > 0) return t("status.selfCheck.mismatch", { frames: d.mismatchedFrames });
  if (d.matchedFrames === 0) return t("status.selfCheck.checking");
  const rate = s.outputFormat?.sampleRate ?? 0;
  return t("status.selfCheck.matched", { amount: amountText(d.matchedFrames, rate, rate > 0) });
}

// --- Meters (task 4-4): True Peak and loudness of what goes to the output ---

function levelText(l: Level): string {
  if (l === "-inf") return "-∞";
  return `${l > 0 ? "+" : ""}${l.toFixed(1)}`;
}

function channelValues(values: Level[]): string {
  if (values.length === 1) return levelText(values[0]);
  if (values.length === 2) return `L ${levelText(values[0])} / R ${levelText(values[1])}`;
  return values.map((v, i) => `${i + 1}: ${levelText(v)}`).join(" / ");
}

const meterRunning = (s: PlaybackStatus) => s.meter.state === "measuring" && s.meter.reading !== null;

// The track values (maximum, integrated) belong to the track playing, not to
// the previous one still being measured just after a change.
const meterTrack = (s: PlaybackStatus) =>
  s.meter.state !== "notPcm" && s.meter.reading !== null && s.meter.queueIndex === s.queueIndex
    ? s.meter.reading
    : null;

// True Peak now (about the last 0.1 s), per channel.
export function meterPeakText(s: PlaybackStatus): string {
  if (s.meter.state === "notPcm") return t("status.meter.notPcm");
  if (!meterRunning(s)) return "—";
  return `${channelValues(s.meter.reading!.truePeakRecent)} dBTP`;
}

// The largest True Peak since the track started.
export function meterTrackPeakText(s: PlaybackStatus): string {
  const r = meterTrack(s);
  if (!r) return "—";
  const over = r.truePeakMax.some((v) => v !== "-inf" && v > 0);
  const values = channelValues(r.truePeakMax);
  return over ? t("status.meter.over", { values }) : `${values} dBTP`;
}

// Momentary (400 ms) and short-term (3 s) loudness.
export function meterLoudnessText(s: PlaybackStatus): string {
  if (s.meter.state === "notPcm" || !meterRunning(s)) return "—";
  const r = s.meter.reading!;
  return t("status.meter.loudness", { momentary: levelText(r.momentaryLufs), shortTerm: levelText(r.shortTermLufs) });
}

// The integrated loudness since the track started.
export function meterIntegratedText(s: PlaybackStatus): string {
  const r = meterTrack(s);
  if (!r) return "—";
  const rate = s.outputFormat?.sampleRate ?? 0;
  const span = rate > 0 ? t("status.meter.span", { duration: durationText(r.trackFrames / rate) }) : "";
  if (r.integratedLufs === "-inf") return t("status.meter.integratedWaiting", { span });
  return `${levelText(r.integratedLufs)} LUFS${span}`;
}

// What the meters measure, and data they could not measure.
export function meterNote(s: PlaybackStatus): string {
  const rate = s.outputFormat?.sampleRate ?? 0;
  const dropped = s.meter.droppedFrames;
  if (dropped === 0) return t("status.meter.note");
  return t("status.meter.noteDropped", { amount: amountText(dropped, rate, rate > 0 && dropped >= rate) });
}

// Checksum check of the current track (FLAC stores the MD5 of its samples).
export function integrityText(s: PlaybackStatus): string {
  const c = s.integrity;
  switch (c.state) {
    case "passed":
      return t("status.integrity.passed");
    case "pending":
      return t("status.integrity.pending");
    case "skipped":
      return t("status.integrity.skipped");
    case "failed":
      return c.decodeErrors > 0
        ? t("status.integrity.decodeErrors", { count: c.decodeErrors })
        : t("status.integrity.failed");
    case "notAvailable":
      return t("status.integrity.notAvailable");
  }
}

export function integrityFailuresText(s: PlaybackStatus): string {
  if (s.integrityFailures.length === 0) return "";
  return t("status.integrity.failures", { tracks: list(s.integrityFailures.map((i) => String(i + 1))) });
}

export function dropoutText(d: DropoutCounts): string {
  const total = d.lateBuffers + d.deviceTimeouts + d.readStarvations;
  if (total === 0) return t("status.dropout.count", { count: 0 });
  const parts: string[] = [];
  if (d.lateBuffers > 0) parts.push(t("status.dropout.late", { count: d.lateBuffers }));
  if (d.readStarvations > 0) parts.push(t("status.dropout.read", { count: d.readStarvations }));
  if (d.deviceTimeouts > 0) parts.push(t("status.dropout.timeout", { count: d.deviceTimeouts }));
  return t("status.dropout.detail", { count: total, parts: list(parts) });
}

export function priorityText(realtime: boolean): string {
  return realtime ? t("status.priority.realtime") : t("status.priority.normal");
}

export function repackText(s: PlaybackStatus): string {
  const src = s.sourceFormat;
  const out = s.outputFormat;
  switch (s.repack) {
    case "none":
      return t("status.repack.none");
    case "widerContainer":
      return src && out
        ? t("status.repack.wider", { from: src.containerBits, to: out.containerBits, valid: out.validBits })
        : t("status.repack.widerUnknown");
    case "zeroPadded":
      return src && out
        ? t("status.repack.zeroPadded", { from: src.validBits, to: out.validBits, pad: out.validBits - src.validBits })
        : t("status.repack.zeroPaddedUnknown");
  }
}

const OUTPUT_ERROR_KEYS: Record<string, MessageKey> = {
  deviceNotFound: "status.error.deviceNotFound",
  deviceInUse: "status.error.deviceInUse",
  exclusiveModeNotAllowed: "status.error.exclusiveModeNotAllowed",
  formatNotSupported: "status.error.formatRejected",
  deviceInvalidated: "status.error.deviceInvalidated",
  timeout: "status.error.timeout",
};

export function errorText(s: PlaybackStatus): string {
  const e = s.error;
  if (e.kind === "none") return "";
  const fmt = s.sourceFormat;
  const rate = fmt ? rateText(fmt.sampleRate) : t("status.error.unknownRate");

  if (e.kind === "formatNotSupported") {
    switch (e.reason) {
      case "sampleRate": {
        const rates = e.acceptedRates ?? [];
        const accepted = rates.length > 0 ? t("status.error.acceptedRates", { rates: list(rates.map(rateText)) }) : "";
        return t("status.error.sampleRate", { rate, accepted });
      }
      case "bitDepth":
        return t("status.error.bitDepth", { rate, format: fmt ? describeFormat(fmt) : "" });
      case "channels":
        return t("status.error.channels", { channels: fmt?.channels ?? "" });
      case "sampleType":
        return fmt?.sampleType === "int" ? t("status.error.floatOnly") : t("status.error.sampleType");
      default:
        return t("status.error.format");
    }
  }

  if (e.kind === "output") {
    // The app's own reason (task 6-3: an ASIO driver that failed its check, a driver that is gone).
    if (s.outputNotice) return nativeText(s.outputNotice);
    const key = e.outputError ? OUTPUT_ERROR_KEYS[e.outputError] : undefined;
    return key ? t(key) : t("status.error.device", { detail: e.detail });
  }

  if (e.kind === "load") return t("status.error.load", { track: s.queueIndex + 1, detail: e.detail });

  if (e.kind === "dopNotAllowed") return t("status.error.dopNotAllowed", { track: s.queueIndex + 1 });

  return t("status.error.other", { detail: e.detail });
}

// --- The output field at the right end of the top bar (CLAUDE.md chapter 8) ---

export interface OutputTag {
  text: string;
  kind: "plain" | "ok" | "warn";
}

// Short tags: the container, the rate, the bit depth, then BIT-PERFECT as
// judged from the actual path (task 4-2), or the first reason it is not.
export function outputTags(s: PlaybackStatus): OutputTag[] {
  const f = s.outputFormat;
  if (!f || !s.path) return [{ text: "—", kind: "plain" }];
  const tags: OutputTag[] = [];
  if (s.path.container) tags.push({ text: s.path.container, kind: "plain" });
  if (s.dop) {
    tags.push({ text: dsdRate(s.dop.dsdRate).name, kind: "plain" });
    tags.push({ text: "DoP", kind: "plain" });
  } else {
    tags.push({ text: `${Number((f.sampleRate / 1000).toFixed(1))} kHz`, kind: "plain" });
    tags.push({ text: f.sampleType === "float" ? "32bit float" : `${f.validBits} bit`, kind: "plain" });
  }
  const v = s.bitPerfect;
  if (!v) return tags;
  if (v.bitPerfect) {
    tags.push({ text: "BIT-PERFECT", kind: "ok" });
    return tags;
  }
  const volume = s.stages.find((st) => st.name === "volume");
  if (volume && volume.gainDb !== null) tags.push({ text: `DIGITAL VOL ${dbText(volume.gainDb)}`, kind: "warn" });
  else if (v.comparison) tags.push({ text: t("status.tag.comparison"), kind: "warn" });
  else if (v.deliveryMismatch) tags.push({ text: t("status.tag.mismatch"), kind: "warn" });
  else if (s.stages.some((st) => st.name === "resampler") && s.stages.every((st) => st.name === "resampler" || st.name === "limiter"))
    tags.push({ text: t("status.tag.resample"), kind: "warn" }); // task 10-4: the conversion is all that runs
  else tags.push({ text: t("status.tag.processing"), kind: "warn" });
  return tags;
}
