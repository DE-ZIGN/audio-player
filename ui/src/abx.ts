// The ABX screen's data and texts (task 6-6). The test itself runs in the
// engine (AbxTest, task 4-1): results stay hidden until the last trial,
// "distinguished" means p < 0.05, and the texts never say which sounds better.

import { FLAT_PRESET, presetFrom, type EqPreset } from "./eqPresets";
import { TUBE_LABELS, type ListeningSettings, type TubeType } from "./settings";
import { unitLabel } from "./tubeCharacter";

export type AbxKind = "processed" | "16bit" | "presets";
export type AbxChoice = "A" | "B" | "X";

export interface AbxResultInfo {
  finished: boolean;
  aborted: boolean;
  trials: number;
  correct: number | null; // only when finished
  pValue: number | null; // only when finished
  distinguished: boolean;
  levelDifferenceDb: number | null;
  levelMatched: boolean;
}

export interface AbxStatus {
  running: boolean;
  kind: AbxKind;
  trial: number; // from 1
  trials: number;
  answered: number;
  listening: AbxChoice;
  startFrames: number;
  lengthFrames: number;
  rate: number;
  matchDb: number; // the gain on B to match A's loudness
  aName?: string; // 設定 A と設定 B: the presets' names
  bName?: string;
  result: AbxResultInfo | null; // when it has ended
}

export interface AbxRequest {
  kind: AbxKind;
  startSeconds: number;
  seconds: number;
  trials: number;
  a?: SideRequest; // 設定 A と設定 B (task 6-9; the tone and the tube, task 9-4)
  b?: SideRequest;
}

export const KINDS: Record<AbxKind, { title: string; a: string; b: string }> = {
  processed: { title: "原音 と 音作り（今の設定）", a: "原音", b: "音作り" },
  "16bit": { title: "原音 と 16bit（TPDF ディザ）", a: "原音", b: "16bit" },
  presets: { title: "設定 A と 設定 B（EQ・トーン・真空管）", a: "設定 A", b: "設定 B" },
};

// What the A and B buttons and the run's description call the two sides.
export function sideLabels(a: { kind: AbxKind; aName?: string; bName?: string }) {
  if (a.kind === "presets" && a.aName && a.bName)
    return { a: a.aName, b: a.bName, title: `設定 A「${a.aName}」と 設定 B「${a.bName}」` };
  return { ...KINDS[a.kind] };
}

// --- 設定 A と設定 B (tasks 6-9, 9-4) ----------------------------------------------------
// Each side: an EQ (the current one, none, or a saved preset), the tone (the
// current one or none) and the tube (none or a type, with DRIVE). Everything
// else is as it is now, in 音作りモード.

export type SideTube = "none" | TubeType;

export type SideWarmUp = "off" | "now" | "cold"; // task 9-8: none, as it is now, held at switch-on

export interface SideChoice {
  eq: string; // "current" | "flat" | "p:<preset name>"
  tone: "current" | "flat";
  tube: SideTube;
  drive: number; // dB, with a tube
  warmUp?: SideWarmUp; // with a tube (none when left out)
  unit?: number; // with a tube: the unit's number, 0 (or left out) for the type's own tubes
}

export interface SideRequest extends EqPreset {
  toneBassDb?: number;
  toneTrebleDb?: number;
  tubeEnabled?: boolean;
  tubeType?: TubeType;
  tubeDriveDb?: number;
  tubeWarmUp?: boolean;
  tubeWarmUpCold?: boolean; // the warm-up held at switch-on for this side
  tubeUnits?: boolean;
  tubeUnit?: number;
}

const TUBE_ORDER: TubeType[] = ["se300b", "ppel34", "ppclassa"];

// A: as it is now. B: the same with another tube type (or with the tube, if A has none).
export function defaultSides(l: ListeningSettings): { a: SideChoice; b: SideChoice } {
  const tube: SideTube = l.tubeEnabled ? l.tubeType : "none";
  const a: SideChoice = {
    eq: "current",
    tone: "current",
    tube,
    drive: l.tubeDriveDb,
    warmUp: l.tubeWarmUp ? "now" : "off",
    unit: l.tubeUnits ? l.tubeUnit : 0,
  };
  const other: SideTube = tube === "none" ? "se300b" : TUBE_ORDER[(TUBE_ORDER.indexOf(tube) + 1) % TUBE_ORDER.length];
  return { a, b: { ...a, tube: other } };
}

const db = (v: number) => `${v > 0 ? "+" : ""}${v.toFixed(1)} dB`;

export function sideRequest(c: SideChoice, l: ListeningSettings, presets: EqPreset[]): SideRequest {
  const eq =
    c.eq === "current"
      ? presetFrom(l, "今の EQ")
      : (presets.find((p) => `p:${p.name}` === c.eq) ?? { ...FLAT_PRESET, name: "EQ なし" });
  const bass = c.tone === "current" ? l.toneBassDb : 0;
  const treble = c.tone === "current" ? l.toneTrebleDb : 0;
  const parts = [eq.name];
  const tone = [bass !== 0 ? `BASS ${db(bass)}` : "", treble !== 0 ? `TREBLE ${db(treble)}` : ""].filter((t) => t);
  if (tone.length > 0) parts.push(`トーン（${tone.join("、")}）`);
  const warmUp = c.warmUp ?? "off";
  const unit = c.unit ?? 0;
  const show = [
    warmUp === "now" ? "・ウォームアップ：今の状態" : warmUp === "cold" ? "・ウォームアップ：起動直後" : "",
    unit > 0 ? `・${unitLabel(unit)}` : "",
  ].join("");
  parts.push(c.tube === "none" ? "真空管なし" : `${TUBE_LABELS[c.tube]}（DRIVE ${db(c.drive)}${show}）`);
  return {
    ...structuredClone(eq),
    name: parts.join("・"),
    toneBassDb: bass,
    toneTrebleDb: treble,
    tubeEnabled: c.tube !== "none",
    tubeType: c.tube === "none" ? l.tubeType : c.tube,
    tubeDriveDb: c.drive,
    tubeWarmUp: warmUp !== "off",
    tubeWarmUpCold: warmUp === "cold",
    tubeUnits: unit > 0,
    tubeUnit: unit > 0 ? unit : l.tubeUnit,
  };
}

// "" when the two sides can be compared.
export function sidePairProblem(a: SideChoice, b: SideChoice): string {
  const sameTube =
    a.tube === "none" ||
    (a.drive === b.drive && (a.warmUp ?? "off") === (b.warmUp ?? "off") && (a.unit ?? 0) === (b.unit ?? 0));
  const same = a.eq === b.eq && a.tone === b.tone && a.tube === b.tube && sameTube;
  return same ? "A と B に違う設定を選んでください。" : "";
}

function choose(n: number, k: number): number {
  let c = 1;
  for (let i = 1; i <= k; i++) c = (c * (n - k + i)) / i;
  return c;
}

// P(at least `correct` of `trials` by guessing): the p of the test.
export function upperTailP(correct: number, trials: number): number {
  let sum = 0;
  for (let k = Math.max(0, correct); k <= trials; k++) sum += choose(trials, k);
  return Math.min(1, sum / Math.pow(2, trials));
}

// P(at most `correct` by guessing): for the "too few correct" note.
export function lowerTailP(correct: number, trials: number): number {
  return upperTailP(trials - correct, trials);
}

const signed = (x: number, digits: number) => {
  const r = Number(x.toFixed(digits));
  return r === 0 ? `+${(0).toFixed(digits)}` : `${r > 0 ? "+" : "−"}${Math.abs(r).toFixed(digits)}`;
};

export interface ResultText {
  headline: string;
  p: string;
  verdict: string;
  level: string;
  low: string; // "too few correct", or ""
}

export function resultText(r: AbxResultInfo): ResultText {
  const level =
    r.levelDifferenceDb === null
      ? "音量差：測れませんでした（流した時間が短すぎます。比べ始めの 3 秒は測りません）"
      : `音量差 ${signed(r.levelDifferenceDb, 2)} dB（基準は 0.1 dB 以内）` +
        (r.levelMatched ? "" : "：基準を超えたので、結果は参考です（音量の違いで聞き分けた可能性があります）");
  if (!r.finished || r.correct === null || r.pValue === null)
    return {
      headline: "中断しました",
      p: "",
      verdict: "途中で中断したので、結果は出しません（途中の結果で判断すると、p の意味が崩れるため）。",
      level: "",
      low: "",
    };
  const verdict = r.distinguished
    ? "p < 0.05：偶然とは考えにくい結果です。A と B の違いを聞き分けられた可能性が高いと言えます。"
    : "p ≥ 0.05：偶然でも十分に起こりうる結果です。聞き分けられたとは言えません（違いがないという意味ではありません）。";
  const lower = lowerTailP(r.correct, r.trials);
  const low =
    lower < 0.05
      ? `正解が少なすぎます（当てずっぽうでこれ以下になる確率は ${(lower * 100).toFixed(1)}%）。` +
        "A と B の違いは聞こえていて、取り違えている可能性があります。" +
        "この検定は「当たりが多いか」だけを見るので、これを「聞き分けられた」とは数えません。もう一度試すと確かめられます。"
      : "";
  return {
    headline: `${r.trials} 回中 ${r.correct} 回正解`,
    p: `p = ${r.pValue.toFixed(4)}（当てずっぽうでこれ以上当たる確率）`,
    verdict,
    level,
    low,
  };
}

function clock(seconds: number): string {
  const s = Math.floor(seconds);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

export function regionText(startFrames: number, lengthFrames: number, rate: number): string {
  if (rate <= 0) return "";
  const a = startFrames / rate, len = lengthFrames / rate;
  return `${clock(a)}〜${clock(a + len)}（${Math.round(len)} 秒）をくり返し`;
}

// The region to start with: from the current position, moved back so it fits.
export function defaultRegion(positionSeconds: number, totalSeconds: number, lengthSeconds: number) {
  const length = Math.min(lengthSeconds, Math.floor(totalSeconds));
  const start = Math.max(0, Math.min(Math.floor(positionSeconds), Math.floor(totalSeconds - length)));
  return { start, length };
}

export type KeyAction = AbxChoice | "answerA" | "answerB" | "abort";

export function keyAction(key: string): KeyAction | null {
  switch (key.toLowerCase()) {
    case "a":
      return "A";
    case "b":
      return "B";
    case "x":
      return "X";
    case "1":
      return "answerA";
    case "2":
      return "answerB";
    case "escape":
    case "q":
      return "abort";
    default:
      return null;
  }
}
