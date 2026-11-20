// The output check (task 6-4, CLAUDE.md chapter 8 "出力の確認"): what the
// signal path diagram, the BIT-PERFECT judgement with its reasons, the ISP
// warning and the meters show, worked out from the playback status. The
// texts say what is done and what was measured; never that it sounds better.

import { CROSSFEED_LEVELS, TUBE_LABELS, parseSpaceDetail, spaceText, type CrossfeedLevel } from "./settings";
import { characterParts, parseTubeDetail } from "./tubeCharacter";
import { resampleText } from "./outputRate";
import { amountText, bitsText, dsdRateName, meterNote, stageName } from "./messages";
import { dbText } from "./settings";
import { t, type MessageKey } from "./i18n";
import type { FormatInfo, Level, PlaybackStatus, StageState } from "./status";

export interface PathBox {
  title: string;
  lines: string[];
}

export interface StageRow {
  name: string;
  on: boolean;
  value: string;
  warn?: boolean;
  sub?: string[]; // smaller lines under the row (the tube: what chose the type, the show)
}

export interface ProcessingBox {
  title: string;
  badge: string;
  active: boolean;
  rows: StageRow[];
  note: string;
}

export interface PathModel {
  file: PathBox;
  decoder: PathBox;
  processing: ProcessingBox;
  output: PathBox;
  dac: PathBox;
}

export type CheckState = "ok" | "ng" | "warn" | "info";

export interface Check {
  label: string;
  state: CheckState;
  detail: string;
}

export interface VerdictModel {
  title: string;
  bitPerfect: boolean;
  checks: Check[];
  scope: string;
}

const rateText = (rate: number) => `${Number((rate / 1000).toFixed(3))} kHz`;

function shortFormat(f: FormatInfo): string {
  const bits = bitsText(f);
  const values = { bits: bits.text, rate: rateText(f.sampleRate) };
  return bits.padded ? t("status.path.formatPadded", values) : t("status.path.format", values);
}

// The decoders by container; null: the app's own (自前).
const DECODERS: Record<string, string | null> = {
  FLAC: "libFLAC",
  WAV: null,
  AIFF: null,
  AIFC: null,
  ALAC: "Apple ALAC",
  MP3: "mpg123",
  AAC: "Media Foundation",
  DSF: null,
  DFF: null,
};

function decoderOf(container: string): string {
  if (!Object.prototype.hasOwnProperty.call(DECODERS, container)) return "—";
  return DECODERS[container] ?? t("status.path.decoderOwn");
}

function containerOf(s: PlaybackStatus): string {
  if (s.path?.container) return s.path.container;
  if (s.dop) return s.dop.container;
  const ext = /\.([a-z0-9]+)$/i.exec(s.fileName)?.[1]?.toUpperCase() ?? "";
  return ext === "M4A" ? "" : ext;
}

const gainText = (db: number | "-inf" | null) => (db === null ? "" : db === "-inf" ? t("status.path.muted") : dbText(db));

// --- the diagram -------------------------------------------------------------------

const INTEGRITY_LINES: Record<string, () => string> = {
  passed: () => t("status.path.md5Passed"),
  failed: () => `⚠ ${t("status.path.md5Failed")}`,
  pending: () => t("status.path.md5Pending"),
};

export function pathModel(s: PlaybackStatus): PathModel {
  const src = s.sourceFormat;
  const container = containerOf(s);

  const fileLines: string[] = [];
  if (!src) fileLines.push("—");
  else {
    fileLines.push(container || "—");
    fileLines.push(
      s.dop
        ? t("status.path.dsdRate", { dsd: dsdRateName(s.dop.dsdRate), mhz: Number((s.dop.dsdRate / 1e6).toFixed(4)) })
        : shortFormat(src),
    );
    const integrity = INTEGRITY_LINES[s.integrity.state];
    if (integrity) fileLines.push(integrity());
  }

  const decoderLines = !src
    ? ["—"]
    : [
        decoderOf(container),
        s.dop
          ? t("status.path.dopWrap")
          : src.sampleType === "float"
            ? t("status.format.float", { bits: src.containerBits })
            : t("status.path.integer"),
      ];

  // Processing: the fixed order of the stages (CLAUDE.md chapter 4).
  const p = s.path;
  const active = !!p && p.processing && !s.dop;
  const stage = (id: string) => (active ? s.stages.find((st) => st.name === id) : undefined);
  const row = (
    name: string,
    id: string,
    value: (g: number | "-inf" | null, st: StageState) => string,
    sub?: (st: StageState) => string[],
  ): StageRow => {
    const st = stage(id);
    if (!st) return { name, on: false, value: "OFF" };
    return sub ? { name, on: true, value: value(st.gainDb, st), sub: sub(st) } : { name, on: true, value: value(st.gainDb, st) };
  };
  const preamp = (g: number | "-inf" | null) => (g === null ? "ON" : t("status.path.preamp", { db: gainText(g) }));
  const limiterOn = !!stage("limiter");
  const limiterName = t("status.pathRow.limiter");
  const ditherName = t("status.pathRow.dither");
  const rows: StageRow[] = [
    row("ReplayGain", "replaygain", gainText),
    row("EQ", "eq", preamp),
    row(t("status.pathRow.tone"), "tone", preamp),
    row(
      t("status.pathRow.tube"),
      "tube",
      (g, st) => {
        const d = parseTubeDetail(st.detail);
        const type = d.type ? TUBE_LABELS[d.type] : "ON";
        return g === null ? type : t("status.path.tubeValue", { type, drive: gainText(g) });
      },
      (st) => {
        // What chose the type (task 9-7) and the show (task 9-8), a line each.
        const c = s.tubeAuto?.choice;
        const why = c ? [c.reason === "album" ? t("status.path.byAlbum") : t("status.path.byGenre", { genre: c.matched })] : [];
        const d = parseTubeDetail(st.detail);
        return [...why, ...characterParts(d.warmUpSeconds, d.unit)];
      },
    ),
    row(t("status.pathRow.space"), "space", (_g, st) => {
      const sp = parseSpaceDetail(st.detail);
      return sp ? spaceText(sp.room, sp.mix, sp.source) : "ON";
    }),
    row(t("status.pathRow.crossfeed"), "crossfeed", (_g, st) => CROSSFEED_LEVELS[st.detail as CrossfeedLevel]?.numbers ?? "ON"),
    row(t("status.pathRow.volume"), "volume", gainText),
    row(t("status.pathRow.ispSafe"), "isp-safe", gainText),
    limiterOn
      ? {
          name: limiterName,
          on: true,
          value:
            s.limiter.events > 0
              ? t("status.path.limiterEvents", { count: s.limiter.events, db: dbText(s.limiter.maxReductionDb) })
              : t("status.path.limiterZero"),
          warn: s.limiter.events > 0,
        }
      : { name: limiterName, on: false, value: "OFF" },
    row(t("status.pathRow.resampler"), "resampler", (_g, st) => resampleText(st.detail)), // task 10-4
    active && p.dither ? { name: ditherName, on: true, value: "TPDF" } : { name: ditherName, on: false, value: "OFF" },
  ];
  if (active && p.comparison) rows.push({ name: t("status.pathRow.comparison"), on: true, value: "ON" });
  const badge = !p
    ? "—"
    : s.dop
      ? t("status.path.badgeDop")
      : active
        ? t("status.path.badgeFloat")
        : t("status.path.badgeBypass");
  const note = active && p.processingRequired ? t("status.path.toInteger") : "";

  const outputLines: string[] = [];
  if (!p) outputLines.push("—");
  else {
    const o = p.output;
    outputLines.push(
      o.api === "wasapiExclusive"
        ? t(o.eventDriven ? "status.path.wasapiEvent" : "status.path.wasapi")
        : o.api === "asio"
          ? o.asio
            ? t("status.path.asioDriver", { name: o.asio.driverName })
            : "ASIO"
          : t("status.path.dummy"),
    );
    outputLines.push(s.outputFormat ? shortFormat(s.outputFormat) : "—");
    if (s.dop && s.outputFormat) outputLines.push(t("status.path.dopFrame", { rate: rateText(s.outputFormat.sampleRate) }));
    else if (s.repack !== "none" && !active) outputLines.push(t("status.path.repacked"));
  }

  const dacLines = [s.outputName || "—"];
  const ev = p?.output.endpointVolume;
  if (ev && ev.hardware && (ev.muted || ev.scalar < 1))
    dacLines.push(
      `⚠ ${t("status.path.deviceVolume", { volume: ev.muted ? t("status.path.muted") : `${Math.round(ev.scalar * 100)}%` })}`,
    );

  return {
    file: { title: t("status.path.file"), lines: fileLines },
    decoder: { title: t("status.path.decoder"), lines: decoderLines },
    processing: { title: t("status.path.processing"), badge, active, rows, note },
    output: { title: t("status.path.output"), lines: outputLines },
    dac: { title: "DAC", lines: dacLines },
  };
}

// --- BIT-PERFECT and why -------------------------------------------------------------

// The stages named in the verdict (not the limiter, a safety net).
const VERDICT_STAGES = new Set(["replaygain", "eq", "tone", "tube", "space", "crossfeed", "volume", "isp-safe", "resampler"]);

const EXCLUSIVE_KEYS: Record<string, MessageKey> = {
  wasapiExclusive: "status.verdict.exclusiveWasapi",
  asio: "status.verdict.exclusiveAsio",
};

export function verdictModel(s: PlaybackStatus): VerdictModel | null {
  const v = s.bitPerfect;
  const p = s.path;
  if (!v || !p) return null;
  const processing = p.processing && !s.dop;
  const checks: Check[] = [];

  // The digital volume.
  const volume = t("status.verdict.volume");
  if (s.dop) checks.push({ label: volume, state: "ok", detail: t("status.verdict.volumeDop") });
  else if (s.volumeDb === 0) checks.push({ label: volume, state: "ok", detail: "100%" });
  else
    checks.push({
      label: volume,
      state: "ng",
      detail: s.volumeDb === "-inf" ? t("status.path.muted") : `DIGITAL VOL ${dbText(s.volumeDb)}`,
    });

  // Processing.
  const none = t("status.verdict.processing");
  if (!processing) checks.push({ label: none, state: "ok", detail: t("status.verdict.direct") });
  else {
    const what = s.stages.filter((st) => VERDICT_STAGES.has(st.name)).map((st) => stageName(st.name));
    if (v.quantized) what.push(t("status.stage.quantize"));
    if (v.comparison) what.push(t("status.stage.comparison"));
    if (p.dither) what.push(t("status.stage.dither"));
    checks.push({ label: none, state: "ng", detail: what.join(t("list.separator")) || t("status.verdict.processingPath") });
  }

  // The format sent.
  const out = s.outputFormat;
  const formatLabel = t("status.verdict.format");
  let format: Check;
  if (processing)
    format = {
      label: formatLabel,
      state: "ng",
      detail: out ? t("status.verdict.formatProcessed", { format: shortFormat(out) }) : t("status.verdict.formatProcessedNone"),
    };
  else if (s.dop) format = { label: formatLabel, state: "ok", detail: t("status.verdict.formatDop") };
  else if (s.repack === "widerContainer" && out)
    format = { label: formatLabel, state: "ok", detail: t("status.verdict.formatWider", { bits: out.containerBits }) };
  else if (s.repack === "zeroPadded" && out)
    format = { label: formatLabel, state: "ok", detail: t("status.verdict.formatZeroPadded", { bits: out.validBits }) };
  else if (v.floatSource) format = { label: formatLabel, state: "ok", detail: t("status.verdict.formatFloat") };
  else format = { label: formatLabel, state: "ok", detail: t("status.verdict.formatSame") };
  checks.push(format);

  // Exclusive access to the device.
  const exclusive = EXCLUSIVE_KEYS[p.output.api];
  checks.push(
    exclusive
      ? { label: t("status.verdict.exclusive"), state: "ok", detail: t(exclusive) }
      : { label: t("status.verdict.exclusive"), state: "info", detail: t("status.verdict.exclusiveDummy") },
  );

  // The device's own volume (after the driver).
  const ev = p.output.endpointVolume;
  if (ev) {
    const pct = Math.round(ev.scalar * 100);
    const label = t("status.verdict.windowsVolume");
    checks.push(
      !ev.hardware
        ? { label, state: "ok", detail: t("status.verdict.windowsVolumeSoftware") }
        : ev.muted
          ? { label, state: "warn", detail: t("status.verdict.windowsVolumeMuted") }
          : pct < 100
            ? { label, state: "warn", detail: t("status.verdict.windowsVolumeLowered", { percent: pct }) }
            : { label, state: "ok", detail: "100%" },
    );
  }

  // The self-check (task 4-3).
  const d = s.delivery;
  const selfCheck = t("status.verdict.selfCheck");
  if (processing) checks.push({ label: selfCheck, state: "info", detail: t("status.verdict.selfCheckProcessing") });
  else if (d && d.mismatchedFrames > 0)
    checks.push({ label: selfCheck, state: "ng", detail: t("status.verdict.selfCheckMismatch", { frames: d.mismatchedFrames }) });
  else if (d && d.matchedFrames > 0) {
    const rate = out?.sampleRate ?? 0;
    const amount = amountText(d.matchedFrames, rate, rate > 0);
    checks.push({ label: selfCheck, state: "ok", detail: t("status.verdict.selfCheckMatched", { amount }) });
  } else checks.push({ label: selfCheck, state: "info", detail: t("status.verdict.selfCheckChecking") });

  return {
    title: v.bitPerfect ? "BIT-PERFECT" : t("status.verdict.notBitPerfect"),
    bitPerfect: v.bitPerfect,
    checks,
    scope: t("status.verdict.scope"),
  };
}

// --- the ISP warning (原音モード, CLAUDE.md chapter 4) ----------------------------------

export function ispWarning(s: PlaybackStatus): string | null {
  const tp = s.trackTruePeakDb;
  if (!s.path || s.path.processing || s.dop || tp === null || tp <= 0) return null;
  return t("status.isp.warning", { truePeak: dbText(tp).replace(" dB", " dBTP") });
}

// --- meters -----------------------------------------------------------------------------

export const METER_FLOOR = -60;

export function barPercent(level: Level | undefined, floor = METER_FLOOR): number {
  if (level === undefined || level === "-inf") return 0;
  return Math.max(0, Math.min(100, ((level - floor) / -floor) * 100));
}

function levelText(l: Level | undefined): string {
  if (l === undefined) return "—";
  if (l === "-inf") return "−∞";
  const r = Number(l.toFixed(1));
  return r === 0 ? "0.0" : `${r > 0 ? "+" : "−"}${Math.abs(r).toFixed(1)}`;
}

export interface MeterModel {
  peaks: { label: string; now: string; percent: number; max: string; over: boolean }[];
  loudness: { percent: number; momentary: string; shortTerm: string; integrated: string };
  note: string;
}

export function meterModel(s: PlaybackStatus): MeterModel {
  const m = s.meter;
  const running = m.state === "measuring" && m.reading !== null ? m.reading : null;
  const track = m.state !== "notPcm" && m.reading !== null && m.queueIndex === s.queueIndex ? m.reading : null;
  const channels = (running ?? track)?.channels ?? 2;
  const labels = channels === 2 ? ["L", "R"] : Array.from({ length: channels }, (_, i) => String(i + 1));
  const peaks = labels.map((label, i) => {
    const now = running?.truePeakRecent[i];
    const max = track?.truePeakMax[i];
    const top = max ?? now;
    return {
      label,
      now: running ? levelText(now) : "—",
      percent: barPercent(now),
      max: track ? levelText(max) : "—",
      over: top !== undefined && top !== "-inf" && top > 0,
    };
  });
  return {
    peaks,
    loudness: {
      percent: barPercent(running?.momentaryLufs),
      momentary: running ? levelText(running.momentaryLufs) : "—",
      shortTerm: running ? levelText(running.shortTermLufs) : "—",
      integrated: track ? levelText(track.integratedLufs) : "—",
    },
    note: m.state === "notPcm" ? t("status.meter.notPcm") : meterNote(s),
  };
}
