// The settings screen's data (task 6-3), as app/src/SettingsBridge.cpp sends
// and takes it, and the pure parts of the screen (texts, limits, the curve).

import type { EqPreset } from "./eqPresets";

export type ListeningMode = "original" | "processed";
export type ReplayGainMode = "off" | "track" | "album";
export type IspSafeMode = "off" | "measured" | "fixed";
export type EqFilterType = "peak" | "lowShelf" | "highShelf" | "lowPass" | "highPass" | "bandPass" | "notch";

export interface EqBand {
  type: EqFilterType;
  frequency: number; // Hz
  gainDb: number; // peak and shelves
  q: number;
  enabled: boolean;
}

export interface ListeningSettings {
  mode: ListeningMode;
  replayGain: ReplayGainMode;
  ispSafeOriginal: IspSafeMode;
  ispSafeProcessed: IspSafeMode;
  eqEnabled: boolean;
  eqBands: EqBand[];
  eqAutoPreamp: boolean; // false: eqManualPreampDb is used (what goes over 0 dB is left to the limiter)
  eqManualPreampDb: number; // -30 .. 0
}

export interface OutputChoice {
  kind: "wasapi" | "asio";
  id: string; // WASAPI: endpoint ID ("" = the Windows default); ASIO: the driver's CLSID
  name: string;
  allowDop: boolean;
}

export interface SettingsInfo {
  output: OutputChoice;
  listening: ListeningSettings;
  eqPresets?: EqPreset[]; // task 6-9
}

export interface OutputDevice {
  id: string;
  name: string;
  isDefault?: boolean;
}

export interface OutputsInfo {
  wasapi: OutputDevice[];
  asio: OutputDevice[];
  message?: string;
}

export interface EqInfo {
  sampleRate: number;
  preampDb: number;
  skipped: number; // enabled bands that cannot be made at this rate
}

export interface EqCurve extends EqInfo {
  points: [number, number][]; // [Hz, dB]
}

export interface EqImport {
  ok: boolean;
  cancelled?: boolean;
  message?: string;
  bands?: EqBand[];
  warnings?: string[];
  filePreampDb?: number | null;
}

export interface PresetsResult {
  ok: boolean;
  message?: string;
  eqPresets?: EqPreset[];
}
export interface SetResult {
  ok: boolean;
  message?: string;
  eq?: EqInfo;
}

export const MAX_BANDS = 32;

export const defaultListening: ListeningSettings = {
  mode: "original",
  replayGain: "off",
  ispSafeOriginal: "off",
  ispSafeProcessed: "measured",
  eqEnabled: true,
  eqBands: [],
  eqAutoPreamp: true,
  eqManualPreampDb: 0,
};

export const defaultOutput: OutputChoice = { kind: "wasapi", id: "", name: "", allowDop: false };

// --- texts -----------------------------------------------------------------

export const FILTER_LABELS: Record<EqFilterType, string> = {
  peak: "ピーク",
  lowShelf: "ローシェルフ",
  highShelf: "ハイシェルフ",
  lowPass: "ローパス",
  highPass: "ハイパス",
  bandPass: "バンドパス",
  notch: "ノッチ",
};

export const ISP_LABELS: Record<IspSafeMode, string> = {
  off: "オフ",
  measured: "計測した値で下げる",
  fixed: "固定で −3 dB",
};

export const REPLAY_GAIN_LABELS: Record<ReplayGainMode, string> = {
  off: "オフ",
  track: "トラック（曲ごと）",
  album: "アルバム（アルバムの中の音量差を保つ）",
};

export function hasGain(type: EqFilterType): boolean {
  return type === "peak" || type === "lowShelf" || type === "highShelf";
}

export const PREAMP_LIMITS = { min: -30, max: 0 };

const sign = (x: number) => (x > 0 ? "+" : x < 0 ? "−" : "±");

export function dbText(db: number, digits = 1): string {
  const r = Number(Math.abs(db).toFixed(digits));
  return r === 0 ? `0.${"0".repeat(digits)} dB` : `${sign(db)}${r.toFixed(digits)} dB`;
}

export function frequencyText(hz: number): string {
  return hz >= 1000 ? `${Number((hz / 1000).toFixed(2))} kHz` : `${Number(hz.toFixed(1))} Hz`;
}

// What the chosen mode does, in one line, for the screen.
export function modeSummary(l: ListeningSettings): string {
  if (l.mode === "original") {
    const isp = l.ispSafeOriginal === "off" ? "" : `、ISP セーフ（${ISP_LABELS[l.ispSafeOriginal]}）`;
    return `原音モード：音量 100% なら何も処理しません${isp}。ReplayGain と EQ はかけません（設定は残ります）。`;
  }
  const parts: string[] = [];
  if (l.replayGain !== "off") parts.push(`ReplayGain（${l.replayGain === "track" ? "トラック" : "アルバム"}）`);
  const bands = activeBands(l);
  if (bands > 0) parts.push(`EQ（${bands} バンド）`);
  if (l.ispSafeProcessed !== "off") parts.push(`ISP セーフ（${ISP_LABELS[l.ispSafeProcessed]}）`);
  return parts.length === 0
    ? "音作りモード：かける処理がありません。音量 100% なら原音モードと同じ出力です。"
    : `音作りモード：${parts.join("、")}を 64bit 浮動小数点でかけます。`;
}

// Bands that change the sound: enabled, and not a flat peak or shelf.
export function activeBands(l: ListeningSettings): number {
  if (!l.eqEnabled) return 0;
  return l.eqBands.filter((b) => b.enabled && (!hasGain(b.type) || b.gainDb !== 0)).length;
}

export function outputLabel(o: OutputChoice): string {
  if (o.kind === "asio") return `ASIO：${o.name}`;
  return o.id === "" ? "WASAPI 排他：Windows の既定のデバイス" : `WASAPI 排他：${o.name}`;
}

// --- the output list ---------------------------------------------------------

export interface OutputOption {
  value: string; // "wasapi:<id>" or "asio:<clsid>"
  label: string;
  group: "wasapi" | "asio";
  choice: OutputChoice;
  missing?: boolean;
}

export const outputValue = (o: Pick<OutputChoice, "kind" | "id">) => `${o.kind}:${o.id}`;

// The choices for the output list. The current choice is always there, marked
// when its device is not connected now (it is kept, not silently replaced).
export function outputOptions(info: OutputsInfo, current: OutputChoice): OutputOption[] {
  const def = info.wasapi.find((d) => d.isDefault);
  const options: OutputOption[] = [
    {
      value: "wasapi:",
      label: `Windows の既定のデバイス${def ? `（今は ${def.name}）` : ""}`,
      group: "wasapi",
      choice: { kind: "wasapi", id: "", name: "", allowDop: false },
    },
    ...info.wasapi.map((d) => ({
      value: `wasapi:${d.id}`,
      label: d.name,
      group: "wasapi" as const,
      choice: { kind: "wasapi" as const, id: d.id, name: d.name, allowDop: false },
    })),
    ...info.asio.map((d) => ({
      value: `asio:${d.id}`,
      label: d.name,
      group: "asio" as const,
      choice: { kind: "asio" as const, id: d.id, name: d.name, allowDop: false },
    })),
  ];
  const here = options.some((o) => o.value === outputValue(current));
  // A WASAPI device may come back with another ID (the engine also matches its name).
  const sameName = current.kind === "wasapi" && info.wasapi.some((d) => d.name === current.name);
  if (!here && !sameName) {
    options.push({
      value: outputValue(current),
      label: `${current.name}（今は見つかりません）`,
      group: current.kind,
      choice: { ...current },
      missing: true,
    });
  }
  return options;
}

// The option to show as selected for the current choice.
export function selectedOutputValue(info: OutputsInfo, current: OutputChoice): string {
  const value = outputValue(current);
  if (current.kind === "wasapi" && current.id !== "" && !info.wasapi.some((d) => d.id === current.id)) {
    const byName = info.wasapi.find((d) => d.name === current.name);
    if (byName) return `wasapi:${byName.id}`;
  }
  return value;
}

// --- EQ bands ------------------------------------------------------------------

export const LIMITS = {
  frequency: { min: 10, max: 40000 },
  gainDb: { min: -24, max: 24 },
  q: { min: 0.1, max: 20 },
};

const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));

// A number typed into a band's field: within the limits, or null when it is not a number.
export function parseField(field: keyof typeof LIMITS, text: string): number | null {
  const x = Number(text.trim().replace(/[，,]/g, ".").replace(/[−–]/g, "-"));
  if (text.trim() === "" || !Number.isFinite(x)) return null;
  const { min, max } = LIMITS[field];
  return clamp(x, min, max);
}

// The next band to add: a flat peak between the existing ones, or at 1 kHz.
export function newBand(bands: EqBand[]): EqBand {
  const used = bands.map((b) => b.frequency).sort((a, b) => a - b);
  const candidates = [1000, 250, 4000, 63, 8000, 125, 2000, 500, 16000, 31.5];
  const f = candidates.find((c) => !used.includes(c)) ?? 1000;
  return { type: "peak", frequency: f, gainDb: 0, q: 1, enabled: true };
}

// --- the curve -----------------------------------------------------------------

export interface CurveFrame {
  width: number;
  height: number;
  minHz: number;
  maxHz: number;
  rangeDb: number; // the axis shows -rangeDb .. +rangeDb
}

export function xOf(hz: number, f: CurveFrame): number {
  return (Math.log(hz / f.minHz) / Math.log(f.maxHz / f.minHz)) * f.width;
}

export function yOf(db: number, f: CurveFrame): number {
  return ((f.rangeDb - clamp(db, -f.rangeDb, f.rangeDb)) / (2 * f.rangeDb)) * f.height;
}

// SVG path data for the response points (dB clipped to the frame).
export function curvePath(points: [number, number][], frame: CurveFrame, offsetDb = 0): string {
  return points
    .map(([hz, db], i) => `${i === 0 ? "M" : "L"}${xOf(hz, frame).toFixed(1)} ${yOf(db + offsetDb, frame).toFixed(1)}`)
    .join(" ");
}

// A range that shows the curve: ±6, ±12 or ±24 dB.
export function curveRange(points: [number, number][], offsetDb = 0): number {
  const peak = points.reduce((m, [, db]) => Math.max(m, Math.abs(db), Math.abs(db + offsetDb)), 0);
  return peak <= 5.5 ? 6 : peak <= 11.5 ? 12 : 24;
}
