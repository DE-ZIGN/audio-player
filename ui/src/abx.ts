// The ABX screen's data and texts (task 6-6). The test itself runs in the
// engine (AbxTest, task 4-1): results stay hidden until the last trial,
// "distinguished" means p < 0.05, and the texts never say which sounds better.

import { FLAT_PRESET, presetFrom, type EqPreset } from "./eqPresets";
import { t } from "./i18n";
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

// What each kind compares, and its two sides, in the language in use.
function kindTexts(kind: AbxKind): { title: string; a: string; b: string } {
  switch (kind) {
    case "processed":
      return { title: t("controls.abx.kind.processed"), a: t("controls.abx.side.original"), b: t("controls.abx.side.processed") };
    case "16bit":
      return { title: t("controls.abx.kind.sixteenBit"), a: t("controls.abx.side.original"), b: "16bit" };
    case "presets":
      return { title: t("controls.abx.kind.presets"), a: t("controls.abx.side.a"), b: t("controls.abx.side.b") };
  }
}

// What the A and B buttons and the run's description call the two sides.
export function sideLabels(a: { kind: AbxKind; aName?: string; bName?: string }) {
  if (a.kind === "presets" && a.aName && a.bName)
    return { a: a.aName, b: a.bName, title: t("controls.abx.presetsTitle", { a: a.aName, b: a.bName }) };
  return kindTexts(a.kind);
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
      ? presetFrom(l, t("controls.abx.eq.currentName"))
      : (presets.find((p) => `p:${p.name}` === c.eq) ?? { ...FLAT_PRESET, name: t("controls.abx.eq.none") });
  const bass = c.tone === "current" ? l.toneBassDb : 0;
  const treble = c.tone === "current" ? l.toneTrebleDb : 0;
  const sep = t("controls.abx.side.separator");
  const parts = [eq.name];
  const tone = [bass !== 0 ? `BASS ${db(bass)}` : "", treble !== 0 ? `TREBLE ${db(treble)}` : ""].filter((x) => x);
  if (tone.length > 0) parts.push(t("controls.abx.side.tone", { tone: tone.join(t("list.separator")) }));
  const warmUp = c.warmUp ?? "off";
  const unit = c.unit ?? 0;
  const extras = [
    warmUp === "now" ? t("controls.abx.side.warmUpNow") : warmUp === "cold" ? t("controls.abx.side.warmUpCold") : "",
    unit > 0 ? unitLabel(unit) : "",
  ]
    .filter((x) => x)
    .map((x) => sep + x)
    .join("");
  parts.push(
    c.tube === "none"
      ? t("controls.abx.side.noTube")
      : t("controls.abx.side.tube", { tube: TUBE_LABELS[c.tube], drive: db(c.drive), extras }),
  );
  return {
    ...structuredClone(eq),
    name: parts.join(sep),
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
  return same ? t("controls.abx.problem.samePair") : "";
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
      ? t("controls.abx.result.levelUnmeasured")
      : t(r.levelMatched ? "controls.abx.result.level" : "controls.abx.result.levelOver", {
          db: signed(r.levelDifferenceDb, 2),
        });
  if (!r.finished || r.correct === null || r.pValue === null)
    return {
      headline: t("controls.abx.result.aborted"),
      p: "",
      verdict: t("controls.abx.result.abortedVerdict"),
      level: "",
      low: "",
    };
  const verdict = r.distinguished ? t("controls.abx.result.distinguished") : t("controls.abx.result.notDistinguished");
  const lower = lowerTailP(r.correct, r.trials);
  const low = lower < 0.05 ? t("controls.abx.result.tooFew", { percent: (lower * 100).toFixed(1) }) : "";
  return {
    headline: t("controls.abx.result.headline", { trials: r.trials, correct: r.correct }),
    p: t("controls.abx.result.p", { p: r.pValue.toFixed(4) }),
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
  return t("controls.abx.region", { from: clock(a), to: clock(a + len), seconds: Math.round(len) });
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
