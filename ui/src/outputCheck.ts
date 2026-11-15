// The output check (task 6-4, CLAUDE.md chapter 8 "出力の確認"): what the
// signal path diagram, the BIT-PERFECT judgement with its reasons, the ISP
// warning and the meters show, worked out from the playback status. The
// texts say what is done and what was measured; never that it sounds better.

import { CROSSFEED_LEVELS, TUBE_LABELS, parseSpaceDetail, spaceText, type CrossfeedLevel } from "./settings";
import { characterParts, parseTubeDetail } from "./tubeCharacter";
import { dsdRateName, meterNote } from "./messages";
import { dbText } from "./settings";
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
  let bits: string;
  if (f.sampleType === "float") bits = `${f.containerBits}bit 浮動小数点`;
  else if (f.validBits === f.containerBits) bits = `${f.validBits}bit`;
  else bits = `${f.validBits}bit（${f.containerBits}bit コンテナ）`;
  return `${bits}${bits.endsWith("）") ? "" : " "}/ ${rateText(f.sampleRate)}`;
}

const DECODERS: Record<string, string> = {
  FLAC: "libFLAC",
  WAV: "自前",
  AIFF: "自前",
  AIFC: "自前",
  ALAC: "Apple ALAC",
  MP3: "mpg123",
  AAC: "Media Foundation",
  DSF: "自前",
  DFF: "自前",
};

function containerOf(s: PlaybackStatus): string {
  if (s.path?.container) return s.path.container;
  if (s.dop) return s.dop.container;
  const ext = /\.([a-z0-9]+)$/i.exec(s.fileName)?.[1]?.toUpperCase() ?? "";
  return ext === "M4A" ? "" : ext;
}

const gainText = (db: number | "-inf" | null) => (db === null ? "" : db === "-inf" ? "ミュート" : dbText(db));

function durationText(seconds: number): string {
  const m = Math.floor(seconds / 60);
  const sec = Math.floor(seconds % 60);
  return m > 0 ? `${m} 分 ${sec} 秒` : `${sec} 秒`;
}

// --- the diagram -------------------------------------------------------------------

export function pathModel(s: PlaybackStatus): PathModel {
  const src = s.sourceFormat;
  const container = containerOf(s);

  const fileLines: string[] = [];
  if (!src) fileLines.push("—");
  else {
    fileLines.push(container || "—");
    fileLines.push(
      s.dop ? `${dsdRateName(s.dop.dsdRate)}（${Number((s.dop.dsdRate / 1e6).toFixed(4))} MHz）` : shortFormat(src),
    );
    const integrity = { passed: "MD5 一致", failed: "⚠ MD5 不一致", pending: "MD5 照合中" } as Record<string, string>;
    if (integrity[s.integrity.state]) fileLines.push(integrity[s.integrity.state]);
  }

  const decoderLines = !src
    ? ["—"]
    : [
        DECODERS[container] ?? "—",
        s.dop ? "DoP に包む（値はそのまま）" : src.sampleType === "float" ? `${src.containerBits}bit 浮動小数点` : "整数のまま",
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
  const limiterOn = !!stage("limiter");
  const rows: StageRow[] = [
    row("ReplayGain", "replaygain", gainText),
    row("EQ", "eq", (g) => (g === null ? "ON" : `プリアンプ ${gainText(g)}`)),
    row("トーン", "tone", (g) => (g === null ? "ON" : `プリアンプ ${gainText(g)}`)),
    row(
      "真空管",
      "tube",
      (g, st) => {
        const d = parseTubeDetail(st.detail);
        const type = d.type ? TUBE_LABELS[d.type] : "ON";
        return g === null ? type : `${type}・DRIVE ${gainText(g)}`;
      },
      (st) => {
        // What chose the type (task 9-7) and the show (task 9-8), a line each.
        const c = s.tubeAuto?.choice;
        const why = c ? [c.reason === "album" ? "アルバムの指定で選択" : `ジャンル（${c.matched}）で選択`] : [];
        const d = parseTubeDetail(st.detail);
        return [...why, ...characterParts(d.warmUpSeconds, d.unit)];
      },
    ),
    row("空間", "space", (_g, st) => {
      const sp = parseSpaceDetail(st.detail);
      return sp ? spaceText(sp.room, sp.mix, sp.source) : "ON";
    }),
    row("クロスフィード", "crossfeed", (_g, st) => CROSSFEED_LEVELS[st.detail as CrossfeedLevel]?.numbers ?? "ON"),
    row("デジタル音量", "volume", gainText),
    row("ISP セーフ", "isp-safe", gainText),
    limiterOn
      ? {
          name: "保険のリミッター",
          on: true,
          value: s.limiter.events > 0 ? `${s.limiter.events} 回・最大 ${dbText(s.limiter.maxReductionDb)}` : "0 回",
          warn: s.limiter.events > 0,
        }
      : { name: "保険のリミッター", on: false, value: "OFF" },
    active && p.dither ? { name: "ディザ", on: true, value: "TPDF" } : { name: "ディザ", on: false, value: "OFF" },
  ];
  if (active && p.comparison) rows.push({ name: "聴き比べ", on: true, value: "ON" });
  const badge = !p ? "—" : s.dop ? "DoP：処理をかけられません" : active ? "64bit 浮動小数点" : "BYPASS（原音経路）";
  const note = active && p.processingRequired ? "浮動小数点の曲を整数にする（このデバイスは浮動小数点を受け付けないため）" : "";

  const outputLines: string[] = [];
  if (!p) outputLines.push("—");
  else {
    const o = p.output;
    outputLines.push(
      o.api === "wasapiExclusive"
        ? `WASAPI 排他${o.eventDriven ? "（イベント駆動）" : ""}`
        : o.api === "asio"
          ? `ASIO${o.asio ? `（${o.asio.driverName}）` : ""}`
          : "ダミー出力（ファイル）",
    );
    outputLines.push(s.outputFormat ? shortFormat(s.outputFormat) : "—");
    if (s.dop && s.outputFormat) outputLines.push(`DoP（PCM ${rateText(s.outputFormat.sampleRate)} の枠）`);
    else if (s.repack !== "none" && !active) outputLines.push("詰め替え（値は同じ）");
  }

  const dacLines = [s.outputName || "—"];
  const ev = p?.output.endpointVolume;
  if (ev && ev.hardware && (ev.muted || ev.scalar < 1))
    dacLines.push(`⚠ 機器の音量 ${ev.muted ? "ミュート" : `${Math.round(ev.scalar * 100)}%`}`);

  return {
    file: { title: "ファイル", lines: fileLines },
    decoder: { title: "デコード", lines: decoderLines },
    processing: { title: "処理", badge, active, rows, note },
    output: { title: "出力", lines: outputLines },
    dac: { title: "DAC", lines: dacLines },
  };
}

// --- BIT-PERFECT and why -------------------------------------------------------------

const STAGE_NAMES: Record<string, string> = {
  replaygain: "ReplayGain",
  eq: "パラメトリック EQ",
  tone: "トーン",
  tube: "真空管",
  space: "空間",
  crossfeed: "クロスフィード",
  volume: "デジタル音量",
  "isp-safe": "ISP セーフ",
};

export function verdictModel(s: PlaybackStatus): VerdictModel | null {
  const v = s.bitPerfect;
  const p = s.path;
  if (!v || !p) return null;
  const processing = p.processing && !s.dop;
  const checks: Check[] = [];

  // The digital volume.
  if (s.dop) checks.push({ label: "音量 100%", state: "ok", detail: "DoP には音量をかけません" });
  else if (s.volumeDb === 0) checks.push({ label: "音量 100%", state: "ok", detail: "100%" });
  else
    checks.push({
      label: "音量 100%",
      state: "ng",
      detail: s.volumeDb === "-inf" ? "ミュート" : `DIGITAL VOL ${dbText(s.volumeDb)}`,
    });

  // Processing.
  if (!processing) checks.push({ label: "処理なし", state: "ok", detail: "原音経路（処理のコードを通りません）" });
  else {
    const what = s.stages.filter((st) => STAGE_NAMES[st.name]).map((st) => STAGE_NAMES[st.name]);
    if (v.quantized) what.push("浮動小数点の曲を整数に変換");
    if (v.comparison) what.push("聴き比べ");
    if (p.dither) what.push("TPDF ディザ");
    checks.push({ label: "処理なし", state: "ng", detail: what.join("、") || "処理経路" });
  }

  // The format sent.
  const out = s.outputFormat;
  let format: Check;
  if (processing) format = { label: "形式がファイルと一致", state: "ng", detail: `処理経路の形式${out ? `（${shortFormat(out)}）` : ""}で送ります` };
  else if (s.dop) format = { label: "形式がファイルと一致", state: "ok", detail: "DoP（DSD のビットはそのまま）" };
  else if (s.repack === "widerContainer" && out)
    format = { label: "形式がファイルと一致", state: "ok", detail: `${out.containerBits}bit コンテナに詰め替え（値は同じ）` };
  else if (s.repack === "zeroPadded" && out)
    format = { label: "形式がファイルと一致", state: "ok", detail: `下位を 0 で埋めて ${out.validBits}bit に（値は同じ）` };
  else if (v.floatSource) format = { label: "形式がファイルと一致", state: "ok", detail: "デコードした浮動小数点の値のまま" };
  else format = { label: "形式がファイルと一致", state: "ok", detail: "ファイルの形式のまま" };
  checks.push(format);

  // Exclusive access to the device.
  const api = p.output.api;
  checks.push(
    api === "wasapiExclusive"
      ? { label: "排他モード", state: "ok", detail: "WASAPI 排他（ほかのアプリの音は混ざりません）" }
      : api === "asio"
        ? { label: "排他モード", state: "ok", detail: "ASIO（Windows のミキサーを通りません）" }
        : { label: "排他モード", state: "info", detail: "ダミー出力（ファイルに書き出すだけ）" },
  );

  // The device's own volume (after the driver).
  const ev = p.output.endpointVolume;
  if (ev) {
    const pct = Math.round(ev.scalar * 100);
    checks.push(
      !ev.hardware
        ? { label: "Windows の音量", state: "ok", detail: "ソフトウェアの音量なので、排他モードではかかりません" }
        : ev.muted
          ? { label: "Windows の音量", state: "warn", detail: "ミュート（DAC の中でかかります）" }
          : pct < 100
            ? { label: "Windows の音量", state: "warn", detail: `${pct}%（DAC に届いたあとで信号が変わります。この判定はドライバーまで）` }
            : { label: "Windows の音量", state: "ok", detail: "100%" },
    );
  }

  // The self-check (task 4-3).
  const d = s.delivery;
  if (processing) checks.push({ label: "自己確認", state: "info", detail: "処理経路では行いません（値を変える経路のため）" });
  else if (d && d.mismatchedFrames > 0)
    checks.push({ label: "自己確認", state: "ng", detail: `${d.mismatchedFrames} フレームが、ファイルの値と違っていました` });
  else if (d && d.matchedFrames > 0) {
    const rate = out?.sampleRate ?? 0;
    const amount = rate > 0 ? `${durationText(d.matchedFrames / rate)}分` : `${d.matchedFrames} フレーム分`;
    checks.push({ label: "自己確認", state: "ok", detail: `${amount}、ドライバーに渡した値がファイルの値と一致` });
  } else checks.push({ label: "自己確認", state: "info", detail: "確認中（データを渡すと結果が出ます）" });

  return {
    title: v.bitPerfect ? "BIT-PERFECT" : "BIT-PERFECT ではありません",
    bitPerfect: v.bitPerfect,
    checks,
    scope: "判定と自己確認の範囲は、アプリからドライバーまでです。ドライバーの中、USB などの転送、DAC の中は確かめられません。",
  };
}

// --- the ISP warning (原音モード, CLAUDE.md chapter 4) ----------------------------------

export function ispWarning(s: PlaybackStatus): string | null {
  const tp = s.trackTruePeakDb;
  if (!s.path || s.path.processing || s.dop || tp === null || tp <= 0) return null;
  return (
    `この曲の True Peak は ${dbText(tp).replace(" dB", " dBTP")} で、0 dBTP を超えています（取り込み時に測った値）。` +
    "DAC がオーバーサンプリングするとき、サンプルの間のピークがクリップすることがあります。" +
    "ISP セーフをオンにすると、−0.5 dBTP 以下になるように音量を線形に下げます（処理経路になるので、BIT-PERFECT ではなくなります）。"
  );
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
    note:
      m.state === "notPcm"
        ? "DoP の経路では測りません（DSD のデータで、PCM の音の値ではないため）"
        : meterNote(s),
  };
}
