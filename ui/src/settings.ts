// The settings screen's data (task 6-3), as app/src/SettingsBridge.cpp sends
// and takes it, and the pure parts of the screen (texts, limits, the curve).

import type { EqPreset } from "./eqPresets";
import type { TubeAutoSettings, TubeGenreRule } from "./tubeAuto";
import { unitLabel } from "./tubeCharacter";
import { outputRateSummary, type OutputRateMode } from "./outputRate";

export type ListeningMode = "original" | "processed";
export type ReplayGainMode = "off" | "track" | "album";
export type IspSafeMode = "off" | "measured" | "fixed";
export type TubeType = "se300b" | "ppel34" | "ppclassa";
export type CrossfeedLevel = "default" | "cmoy" | "jmeier";
export type SpaceRoom = "off" | "club" | "hall" | "arena";
export type SpaceIrSource = "measured" | "synth" | "file";
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
  // The tone controls and the tube (tasks 9-2, 9-3), in 音作りモード only.
  toneBassDb: number; // -10 .. +10
  toneTrebleDb: number;
  tubeEnabled: boolean;
  tubeType: TubeType;
  tubeDriveDb: number; // -30 .. +6
  // The warm-up and the tubes' differences (task 9-8, a show; off by default).
  // The output rate (task 10-4), in 音作りモード only.
  outputRateMode: OutputRateMode;
  outputRateHz: number; // 固定 and 下限
  outputRateMultiple: number; // 整数倍: 2, 4, 8
  tubeWarmUp: boolean;
  tubeUnits: boolean;
  tubeUnit: number; // 1 .. 99, kept while tubeUnits is off
  // Crossfeed for headphones (task 9-5), in 音作りモード only.
  crossfeedEnabled: boolean;
  crossfeedLevel: CrossfeedLevel;
  // The space (task 9-6), in 音作りモード only.
  spaceRoom: SpaceRoom;
  spaceMixPercent: number; // 0 .. 100
  spacePreDelayMs: number; // 0 .. 100 (at least the convolution's block is used)
  spaceLowCutHz: number; // 0 (none) or 20 .. 500
  // Which IR CLUB, HALL, ARENA play (HALL measured, the others synthesized by
  // default), and the user's file for each (used when its source is "file").
  spaceSource: [SpaceIrSource, SpaceIrSource, SpaceIrSource];
  spaceIr: [string, string, string];
}

// What became of a user IR file (task 9-6), as the app reports it.
export interface SpaceIrStatus {
  path: string;
  loaded: boolean;
  error?: string;
  seconds: number;
  sampleRate: number;
  channels: number; // used: 1 or 2
  fileChannels: number;
  truncated: boolean; // longer than 5 s: cut
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
  tubeAuto?: TubeAutoSettings; // task 9-7
  tubeAutoDefaultRules?: TubeGenreRule[];
  spaceIrs?: SpaceIrStatus[]; // task 9-6
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
export interface TubeAutoResult {
  ok: boolean;
  message?: string;
  tubeAuto?: TubeAutoSettings;
}
export interface SetResult {
  ok: boolean;
  message?: string;
  eq?: EqInfo;
  spaceIrs?: SpaceIrStatus[]; // task 9-6
}
export interface IrChoice {
  ok: boolean;
  cancelled?: boolean;
  path?: string;
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
  toneBassDb: 0,
  toneTrebleDb: 0,
  tubeEnabled: false,
  tubeType: "se300b",
  tubeDriveDb: -12,
  outputRateMode: "track",
  outputRateHz: 96000,
  outputRateMultiple: 2,
  tubeWarmUp: false,
  tubeUnits: false,
  tubeUnit: 1,
  crossfeedEnabled: false,
  crossfeedLevel: "default",
  spaceRoom: "off",
  spaceMixPercent: 20,
  spacePreDelayMs: 10,
  spaceLowCutHz: 120,
  spaceSource: ["synth", "measured", "synth"],
  spaceIr: ["", "", ""],
};

// The space's rooms (engine: SpaceStage, room_ir.cpp). The synthesized IRs:
// image-source early reflections, decaying noise in 4 bands; their numbers
// are the design's (the room's size and the RT60 at mid frequencies, 250 Hz -
// 2 kHz; measured in the tests: CLUB 0.72 s, HALL 1.93 s, ARENA 2.99 s).
export const SPACE_ROOMS: Record<Exclude<SpaceRoom, "off">, { label: string; numbers: string; index: number }> = {
  club: { label: "CLUB", numbers: "9 × 14 × 3.5 m・残響 0.75 秒", index: 0 },
  hall: { label: "HALL", numbers: "25 × 40 × 16 m・残響 2.0 秒", index: 1 },
  arena: { label: "ARENA", numbers: "80 × 110 × 30 m・残響 3.0 秒", index: 2 },
};
// The bundled measured IRs (task 9-6, chosen by listening): HALL only.
// St Paul's Hall: published RT60 2.1 s; its 250 Hz - 2 kHz T20 measured 2.13 s.
export const SPACE_MEASURED: Partial<Record<Exclude<SpaceRoom, "off">, { place: string; numbers: string; credit: string }>> = {
  hall: {
    place: "St Paul's Hall（英国、ハダースフィールド大学のコンサートホール）",
    numbers: "残響 2.1 秒",
    credit:
      "St Paul's Hall の IR：MAIR（Microphone Array Impulse Response Library。Hyunkook Lee、Connor Millns、University of Huddersfield Applied Psychoacoustics Lab）、CC BY 4.0（https://creativecommons.org/licenses/by/4.0/）。ステージ正面中央の音源を AB（無指向性マイク 2 本）で録った 2 本を 1 本のステレオにまとめ、直接音の 1 ms 前から使っています。",
  },
};

export function defaultSpaceSource(room: Exclude<SpaceRoom, "off">): SpaceIrSource {
  return SPACE_MEASURED[room] ? "measured" : "synth";
}

// What a room plays (as the engine decides): the user's file when chosen and
// readable (st: what became of it; without it, a path counts as readable),
// else the room's default; measured only where there is one.
export function playingSpaceSource(
  l: ListeningSettings,
  room: Exclude<SpaceRoom, "off">,
  st?: SpaceIrStatus,
): "measured" | "synth" | "user" {
  const i = SPACE_ROOMS[room].index;
  const chosen = l.spaceSource?.[i] ?? defaultSpaceSource(room);
  if (chosen === "file" && l.spaceIr[i] !== "" && (st ? st.loaded : true)) return "user";
  const s = chosen === "file" ? defaultSpaceSource(room) : chosen;
  return s === "measured" && SPACE_MEASURED[room] ? "measured" : "synth";
}

export const SPACE_LIMITS = {
  mix: { min: 0, max: 100, step: 1, defaultValue: 20 },
  preDelay: { min: 0, max: 100, step: 1, defaultValue: 10 },
  lowCut: { min: 20, max: 500, defaultValue: 120 }, // 0: none
};
// The convolution's block (128 frames at 44.1/48 kHz, scaled with the rate):
// the shortest pre-delay, about 2.7-2.9 ms.
export const SPACE_MIN_PRE_DELAY_MS = 2.9;

const SOURCE_TAGS = { synth: "", measured: "（実測）", user: "（自分の IR）" } as const;

// "HALL（実測）・MIX 20%", "CLUB・MIX 20%" (synthesized), "ARENA（自分の IR）・MIX 20%".
export function spaceText(room: SpaceRoom, mixPercent: number, source: "measured" | "synth" | "user" = "synth"): string {
  if (room === "off") return "OFF";
  return `${SPACE_ROOMS[room].label}${SOURCE_TAGS[source]}・MIX ${Math.round(mixPercent)}%`;
}

// The stage's detail from the app: "hall;20" (synthesized), "hall;20;measured" or "hall;20;user".
export function parseSpaceDetail(
  detail: string | undefined,
): { room: SpaceRoom; mix: number; source: "measured" | "synth" | "user" } | null {
  if (!detail) return null;
  const [room, mix, source] = detail.split(";");
  if (!(room in SPACE_ROOMS)) return null;
  return {
    room: room as SpaceRoom,
    mix: Number(mix) || 0,
    source: source === "user" ? "user" : source === "measured" ? "measured" : "synth",
  };
}

// The low cut's text: "120 Hz" or "なし".
export function lowCutText(hz: number): string {
  return hz <= 0 ? "なし" : `${Math.round(hz)} Hz`;
}

// What a built-in IR is: "実測：St Paul's Hall（…）・残響 2.1 秒" or "合成：25 × 40 × 16 m・残響 2.0 秒".
export function builtInIrText(room: Exclude<SpaceRoom, "off">, source: "measured" | "synth"): string {
  const m = SPACE_MEASURED[room];
  return source === "measured" && m ? `実測：${m.place}・${m.numbers}` : `合成：${SPACE_ROOMS[room].numbers}`;
}

// What the settings screen says about the IR a room plays.
export function spaceIrText(l: ListeningSettings, room: Exclude<SpaceRoom, "off">, st: SpaceIrStatus | undefined): string {
  const i = SPACE_ROOMS[room].index;
  const playing = playingSpaceSource(l, room, st);
  if (playing !== "user") {
    const fallback = defaultSpaceSource(room) === "measured" ? "内蔵（実測）" : "内蔵（合成）";
    if (l.spaceSource?.[i] === "file" && l.spaceIr[i] === "")
      return `自分のファイルはまだ選んでいません。${fallback}を使っています`;
    if (l.spaceSource?.[i] === "file" && st && !st.loaded) {
      const name = st.path.split(/[\\/]/).pop() ?? st.path;
      return `⚠ ${name} を読めませんでした（${st.error ?? "理由不明"}）。${fallback}を使っています`;
    }
    return builtInIrText(room, playing);
  }
  if (!st) return l.spaceIr[i].split(/[\\/]/).pop() ?? l.spaceIr[i];
  const name = st.path.split(/[\\/]/).pop() ?? st.path;
  const ch = st.channels === 1 ? "モノラル" : "ステレオ";
  const extra = [
    st.fileChannels > 2 ? `${st.fileChannels}ch のうち最初の 2ch` : "",
    st.truncated ? "5 秒で切りました" : "",
  ].filter((t) => t !== "");
  return `${name}（${st.seconds.toFixed(2)} 秒・${ch}・${(st.sampleRate / 1000).toFixed(1)} kHz${extra.length ? "・" + extra.join("・") : ""}）`;
}

// The crossfeed's levels (engine: CrossfeedStage, the bs2b method): the
// low-pass's cutoff, and how much lower the other side's low frequencies come.
export const CROSSFEED_LEVELS: Record<CrossfeedLevel, { numbers: string; label: string }> = {
  default: { numbers: "700 Hz・4.5 dB", label: "標準（700 Hz・4.5 dB）" },
  cmoy: { numbers: "700 Hz・6.0 dB", label: "やや弱め（700 Hz・6.0 dB）" },
  jmeier: { numbers: "650 Hz・9.5 dB", label: "弱め（650 Hz・9.5 dB）" },
};

// The tone controls and the tube's limits (engine: ToneStage, TubeStage).
export const TONE_MAX_DB = 10;
export const TONE_BASS_HZ = 150;
export const TONE_TREBLE_HZ = 4500;
export const DRIVE_MIN_DB = -30;
export const DRIVE_MAX_DB = 6;
export const DRIVE_DEFAULT_DB = -12;

export const TUBE_LABELS: Record<TubeType, string> = {
  se300b: "SE 300B",
  ppel34: "PP EL34",
  ppclassa: "PP CLASS A",
};

// What each type is (task 9-1), with what it measured: 1 kHz at -6 dBFS, DRIVE 0 dB.
export const TUBE_NOTES: Record<TubeType, string> = {
  se300b: "300B 1 本のシングルエンド（カソードバイアス）。主に 2 次の倍音を加えます（測定例：2 次 −37 dB、3 次 −53 dB）。",
  ppel34: "EL34 2 本のプッシュプル（AB 級、固定バイアス）。偶数次が打ち消し合い、主に 3 次の倍音を加えます（測定例：3 次 −26 dB）。",
  ppclassa: "EL34 2 本のプッシュプル（A 級）。同じ DRIVE なら倍音は少なめです（測定例：3 次 −40 dB）。",
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
export function modeSummary(l: ListeningSettings, tubeAuto = false): string {
  if (l.mode === "original") {
    const isp = l.ispSafeOriginal === "off" ? "" : `、ISP セーフ（${ISP_LABELS[l.ispSafeOriginal]}）`;
    return `原音モード：音量 100% なら何も処理しません${isp}。ReplayGain・EQ・トーン・真空管・空間・クロスフィードはかけません（設定は残ります）。`;
  }
  const parts: string[] = [];
  if (l.replayGain !== "off") parts.push(`ReplayGain（${l.replayGain === "track" ? "トラック" : "アルバム"}）`);
  const bands = activeBands(l);
  if (bands > 0) parts.push(`EQ（${bands} バンド）`);
  const tone = [
    l.toneBassDb !== 0 ? `BASS ${signedDb(l.toneBassDb)}` : "",
    l.toneTrebleDb !== 0 ? `TREBLE ${signedDb(l.toneTrebleDb)}` : "",
  ].filter((t) => t !== "");
  if (tone.length > 0) parts.push(`トーン（${tone.join("、")}）`);
  if (l.tubeEnabled) {
    // The show (task 9-8), after DRIVE.
    const show = [l.tubeWarmUp ? "、ウォームアップの演出" : "", l.tubeUnits ? `、${unitLabel(l.tubeUnit)}` : ""].join("");
    parts.push(
      tubeAuto
        ? `真空管（ジャンルで自動・対応表にないときは ${TUBE_LABELS[l.tubeType]}、DRIVE ${signedDb(l.tubeDriveDb, false)}${show}）`
        : `真空管（${TUBE_LABELS[l.tubeType]}、DRIVE ${signedDb(l.tubeDriveDb, false)}${show}）`,
    );
  }
  if (l.spaceRoom !== "off" && l.spaceMixPercent > 0)
    parts.push(`空間（${spaceText(l.spaceRoom, l.spaceMixPercent, playingSpaceSource(l, l.spaceRoom))}）`);
  if (l.crossfeedEnabled) parts.push(`クロスフィード（${CROSSFEED_LEVELS[l.crossfeedLevel].numbers}）`);
  if (l.ispSafeProcessed !== "off") parts.push(`ISP セーフ（${ISP_LABELS[l.ispSafeProcessed]}）`);
  const rate = outputRateSummary(l); // task 10-4: only when not the track's rate
  if (rate) parts.push(rate);
  return parts.length === 0
    ? "音作りモード：かける処理がありません。音量 100% なら原音モードと同じ出力です。"
    : `音作りモード：${parts.join("、")}を 64bit 浮動小数点でかけます。`;
}

// "+3.0 dB", "-6.0 dB" ("0.0 dB" without a sign when `plus` is false and it is not above 0).
function signedDb(db: number, plus = true): string {
  return `${plus && db > 0 ? "+" : ""}${db.toFixed(1)} dB`;
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
