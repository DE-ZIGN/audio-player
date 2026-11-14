// The amp view's model (task 9-9, CLAUDE.md chapter 8): what its knobs,
// switch, lamps and badge show, from the settings and the playback status.
// Every control stands for a real processing parameter: MODE is 原音モード /
// 音作りモード, DRIVE / BASS / TREBLE / TUBE TYPE / CROSSFEED are the
// settings of the same names, VOLUME is the digital volume. SPACE and MIX
// stand for the space (IR) stage of task 9-6, which is not made yet: they are
// drawn, and do nothing until it is (the user, 2026-11-14).
//
// Positions are the concept image's pixels (UI-AMP-SE-v1, 1672 x 941, read
// as drawn for a 150% screen like the standard view's), the same numbers as
// tools/ui_amp_parts.py. The amp's picture starts under the drawn title bar.

import { outputTags } from "./messages";
import { DRIVE_DEFAULT_DB, DRIVE_MAX_DB, DRIVE_MIN_DB, TONE_MAX_DB, type ListeningSettings, type TubeType } from "./settings";
import type { PlaybackStatus } from "./status";

export const AMP_TOP = 42; // the drawn title bar above it
export const AMP_SIZE = { width: 1672, height: 582 }; // the picture: y 42..624 of the concept image
export const AMP_SCALE = 2 / 3; // shown at 150%: one image pixel is 2/3 of a CSS pixel

// --- knobs -------------------------------------------------------------------------

export interface KnobSpec {
  min: number;
  max: number;
  step: number;
  defaultValue: number;
}

export const SWEEP = 270; // degrees, as the VOLUME knob's scale on the picture

export const KNOB_SPECS = {
  drive: { min: DRIVE_MIN_DB, max: DRIVE_MAX_DB, step: 0.5, defaultValue: DRIVE_DEFAULT_DB },
  bass: { min: -TONE_MAX_DB, max: TONE_MAX_DB, step: 0.5, defaultValue: 0 },
  treble: { min: -TONE_MAX_DB, max: TONE_MAX_DB, step: 0.5, defaultValue: 0 },
  // The volume slider's own scale (0..1, top = 100% = no processing).
  volume: { min: 0, max: 1, step: 0.001, defaultValue: 1 },
  mix: { min: 0, max: 100, step: 1, defaultValue: 0 }, // task 9-6
} satisfies Record<string, KnobSpec>;

export type KnobName = keyof typeof KNOB_SPECS;

const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));

export function snap(spec: KnobSpec, v: number): number {
  const s = Math.round((clamp(v, spec.min, spec.max) - spec.min) / spec.step) * spec.step + spec.min;
  return Number(clamp(s, spec.min, spec.max).toFixed(6));
}

// Where the pointer is: degrees from straight up, clockwise.
export function knobAngle(spec: KnobSpec, v: number): number {
  const n = (clamp(v, spec.min, spec.max) - spec.min) / (spec.max - spec.min);
  return -SWEEP / 2 + n * SWEEP;
}

// A drag up turns it up: 200 px for the whole range (2000 with Shift).
export function dragValue(spec: KnobSpec, start: number, dyPixels: number, fine: boolean): number {
  return snap(spec, start - (dyPixels / (fine ? 2000 : 200)) * (spec.max - spec.min));
}

// The wheel and the arrow keys: one step (a tenth of one with Shift, at least one step).
export function stepValue(spec: KnobSpec, v: number, direction: number, fine: boolean): number {
  const unit = spec === KNOB_SPECS.volume ? 0.01 : spec.step * 2;
  const step = fine ? Math.max(spec.step, unit / 10) : unit;
  return snap(spec, v + Math.sign(direction) * step);
}

export function knobText(name: KnobName, v: number): string {
  if (name === "mix") return "空間（9-6）はまだありません";
  const db = `${v > 0 ? "+" : v < 0 ? "−" : ""}${Math.abs(v).toFixed(1)} dB`;
  return db;
}

// --- layout (image px) -----------------------------------------------------------------

export interface Point {
  x: number;
  y: number;
}

export const KNOBS: Record<KnobName, Point & { r: number }> = {
  drive: { x: 379, y: 459, r: 41 },
  bass: { x: 518, y: 463, r: 31 },
  treble: { x: 632, y: 463, r: 31 },
  volume: { x: 837.5, y: 456.5, r: 66 },
  mix: { x: 1264, y: 455, r: 38 },
};

export type LampName = "pure" | "tube" | "off" | "club" | "hall" | "arena" | TubeType | "crossfeed" | "auto";

export const LAMPS: Record<LampName, Point & { r: number }> = {
  pure: { x: 200.6, y: 429.4, r: 9.5 },
  tube: { x: 200.6, y: 473, r: 9.5 },
  off: { x: 1004, y: 450, r: 9.5 },
  club: { x: 1056, y: 450, r: 9.5 },
  hall: { x: 1108, y: 450, r: 9.5 },
  arena: { x: 1164, y: 450, r: 9.5 },
  se300b: { x: 1397, y: 448, r: 9.5 },
  ppel34: { x: 1463, y: 446, r: 9.5 },
  ppclassa: { x: 1531, y: 446, r: 9.5 },
  // Not on the concept image (it has no place for them on the single-ended
  // panel): small lamps under the SPACE row and beside TUBE TYPE.
  crossfeed: { x: 1012, y: 509, r: 6.5 },
  auto: { x: 1538, y: 407, r: 5.5 },
};

export const TOGGLE = { x: 117, y: 391, width: 66, height: 118 }; // centred on the nut, room for the lever both ways

export interface Word extends Point {
  text: string[];
  size: number; // image px
  align?: "left" | "center";
}

export const WORDS: Word[] = [
  { text: ["MODE"], x: 173, y: 391, size: 19 },
  { text: ["PURE"], x: 222, y: 430, size: 17, align: "left" },
  { text: ["TUBE"], x: 222, y: 473, size: 17, align: "left" },
  { text: ["DRIVE"], x: 380, y: 403, size: 19 },
  { text: ["BASS"], x: 520, y: 413, size: 19 },
  { text: ["TREBLE"], x: 633, y: 413, size: 19 },
  { text: ["VOLUME"], x: 839, y: 366, size: 19 },
  { text: ["SPACE"], x: 1084, y: 413, size: 19 },
  { text: ["OFF"], x: 1003, y: 479, size: 14 },
  { text: ["CLUB"], x: 1056, y: 479, size: 14 },
  { text: ["HALL"], x: 1109, y: 479, size: 14 },
  { text: ["ARENA"], x: 1166, y: 479, size: 14 },
  { text: ["MIX"], x: 1262, y: 405, size: 19 },
  { text: ["TUBE TYPE"], x: 1462, y: 406, size: 19 },
  { text: ["SE", "300B"], x: 1396, y: 482, size: 15 },
  { text: ["PP", "EL34"], x: 1463, y: 482, size: 15 },
  { text: ["PP", "CLASS A"], x: 1534, y: 482, size: 15 },
  { text: ["CROSSFEED"], x: 1023, y: 509, size: 13, align: "left" },
  { text: ["AUTO"], x: 1547, y: 407, size: 11, align: "left" },
];

// The VU meters' paper (the scale is drawn on it) and the channel letters.
export interface VuPlace {
  paper: { x0: number; y0: number; x1: number; y1: number };
  hump: Point & { r: number }; // the black hump over the pivot (drawn over the needle)
  letter: Point;
  pivotY: number; // the needle's pivot, under the paper as on a real meter
  radius: number; // the scale's arc
  labelY: number; // "VU"
}

export const VU: Record<"L" | "R", VuPlace> = {
  L: {
    paper: { x0: 140, y0: 185, x1: 357, y1: 301 },
    hump: { x: 248.5, y: 301, r: 22 },
    letter: { x: 249, y: 322 },
    pivotY: 335,
    radius: 110,
    labelY: 283,
  },
  R: {
    paper: { x0: 1318, y0: 185, x1: 1536, y1: 301 },
    hump: { x: 1427, y: 301, r: 22 },
    letter: { x: 1428, y: 322 },
    pivotY: 335,
    radius: 110,
    labelY: 283,
  },
};

export const BADGE = { x: 288, y: 573, width: 200, height: 30 };
// The round buttons on the wood: prev, play / pause (the amber ring), stop, next.
export const BUTTONS = [
  { x: 670, y: 575, r: 33.5 },
  { x: 779, y: 575, r: 37 },
  { x: 890, y: 575, r: 33.5 },
  { x: 1000, y: 575, r: 33.5 },
];
// Shuffle and repeat (task 9-9): the SE picture has no buttons for them, so
// smaller ones on the wood either side (the PP picture has them there).
export const MODE_BUTTONS = {
  shuffle: { x: 560, y: 575, r: 28 },
  repeat: { x: 1110, y: 575, r: 28 },
};
export const VOLUME_READOUT = { x: 1170, y: 575 };

// --- the two amps -------------------------------------------------------------------------
// The single-ended amp (SE 300B) and the push-pull one (PP EL34, PP CLASS A)
// have panels of their own (the user, 2026-11-14): the amp changes look and
// layout with the tube type. Both have the same controls (CLAUDE.md chapter 8).

export type KeyName = "pure" | "tube" | TubeType | "off" | "club" | "hall" | "arena";

export interface Box {
  x0: number;
  y0: number;
  x1: number;
  y1: number;
}

export type TransportName = "shuffle" | "prev" | "play" | "stop" | "next" | "repeat";

export interface AmpLayout {
  look: "se" | "pp";
  knobs: Record<KnobName, Point & { r: number }>;
  lamps: Partial<Record<LampName, Point & { r: number }>>; // SE: the lamps; PP: AUTO only
  keys: Partial<Record<KeyName, Box>>; // PP: the push buttons (the page lights them and writes their labels)
  modeToggle: Box | null; // SE: MODE is a toggle
  crossfeedToggle: Box | null; // PP: CROSSFEED is a toggle
  words: Word[];
  vu: Record<"L" | "R", VuPlace>;
  badge: { x: number; y: number; width: number; height: number };
  transport: Record<TransportName, Point & { r: number }>;
  readout: Point; // the volume's number and mute
}

const box = (x0: number, y0: number, x1: number, y1: number): Box => ({ x0, y0, x1, y1 });

export const SE_LAYOUT: AmpLayout = {
  look: "se",
  knobs: KNOBS,
  lamps: LAMPS,
  keys: {},
  modeToggle: box(TOGGLE.x, TOGGLE.y, TOGGLE.x + TOGGLE.width, TOGGLE.y + TOGGLE.height),
  crossfeedToggle: null,
  words: WORDS,
  vu: VU,
  badge: BADGE,
  transport: {
    shuffle: MODE_BUTTONS.shuffle,
    prev: BUTTONS[0],
    play: BUTTONS[1],
    stop: BUTTONS[2],
    next: BUTTONS[3],
    repeat: MODE_BUTTONS.repeat,
  },
  readout: VOLUME_READOUT,
};

// From UI-AMP-PP-v1 (tools/ui_amp_pp_parts.py has the same numbers).
export const PP_LAYOUT: AmpLayout = {
  look: "pp",
  knobs: {
    drive: { x: 594, y: 434.5, r: 35.5 },
    bass: { x: 1012, y: 431, r: 30.5 },
    treble: { x: 1117, y: 431, r: 30.5 },
    volume: { x: 835.5, y: 416, r: 76 },
    mix: { x: 1331, y: 465.5, r: 29 },
  },
  lamps: { auto: { x: 431, y: 356, r: 5.5 } },
  keys: {
    pure: box(104, 391, 212, 425),
    tube: box(104, 443, 212, 477),
    se300b: box(301, 377, 441, 413),
    ppel34: box(301, 422, 441, 456),
    ppclassa: box(301, 465, 441, 499),
    off: box(1203, 370, 1263, 402),
    club: box(1269, 370, 1329, 402),
    hall: box(1336, 370, 1396, 402),
    arena: box(1401, 370, 1461, 402),
  },
  modeToggle: null,
  crossfeedToggle: box(1519, 393, 1583, 493), // centred on the nut (y 443): the lever turns over about it
  words: [
    { text: ["MODE"], x: 158, y: 361, size: 19 },
    { text: ["TUBE TYPE"], x: 371, y: 356, size: 19 },
    { text: ["DRIVE"], x: 596, y: 361, size: 19 },
    { text: ["VOLUME"], x: 836, y: 302, size: 19 },
    { text: ["BASS"], x: 1012, y: 367, size: 19 },
    { text: ["TREBLE"], x: 1118, y: 367, size: 19 },
    { text: ["SPACE"], x: 1325, y: 356, size: 19 },
    { text: ["MIX"], x: 1331, y: 418, size: 19 },
    { text: ["CROSSFEED"], x: 1552, y: 366, size: 19 },
    { text: ["AUTO"], x: 439, y: 356, size: 11, align: "left" },
  ],
  vu: {
    L: {
      paper: { x0: 86, y0: 123, x1: 327, y1: 245 },
      hump: { x: 207, y: 245, r: 20 },
      letter: { x: 210, y: 262 },
      pivotY: 283,
      radius: 122,
      labelY: 225,
    },
    R: {
      paper: { x0: 1346, y0: 123, x1: 1587, y1: 245 },
      hump: { x: 1467, y: 245, r: 20 },
      letter: { x: 1466, y: 262 },
      pivotY: 283,
      radius: 122,
      labelY: 225,
    },
  },
  badge: { x: 836.5, y: 519.5, width: 270, height: 28 },
  transport: {
    shuffle: { x: 600.5, y: 576.5, r: 28.5 },
    prev: { x: 710.5, y: 576.5, r: 28.5 },
    play: { x: 836.5, y: 576.5, r: 39 },
    next: { x: 963.5, y: 576.5, r: 28.5 },
    repeat: { x: 1075.5, y: 576.5, r: 28.5 },
    // Not on the picture: stop, small, past the right-hand vent (the user, 2026-11-14).
    stop: { x: 1537, y: 576.5, r: 20 },
  },
  readout: { x: 1564, y: 576.5 },
};

export const LAYOUTS: Record<"se" | "pp", AmpLayout> = { se: SE_LAYOUT, pp: PP_LAYOUT };

// --- what the amp shows -------------------------------------------------------------------

export type AmpLook = "se" | "pp";

export interface Badge {
  text: string;
  kind: "ok" | "tube" | "warn" | "none";
}

export interface AmpState {
  mode: "pure" | "tube"; // the switch and its lamps: the mode chosen
  tubesLit: boolean; // the tube stage is in the path now
  look: AmpLook; // single-ended or push-pull, from the type playing
  type: TubeType; // the TUBE TYPE lamp: the type playing (chosen by genre, task 9-7), else the manual one
  auto: boolean; // the type is chosen by genre
  crossfeed: boolean;
  badge: Badge;
  // Knobs that do nothing in 原音モード are shown disabled there (the layout stays).
  processingControls: boolean;
}

const TUBE_TYPES: TubeType[] = ["se300b", "ppel34", "ppclassa"];

export function playingTubeType(s: PlaybackStatus | null): TubeType | null {
  const st = s?.stages.find((x) => x.name === "tube");
  return st && TUBE_TYPES.includes(st.detail as TubeType) ? (st.detail as TubeType) : null;
}

export function lookOf(type: TubeType): AmpLook {
  return type === "se300b" ? "se" : "pp";
}

// The badge on the wood: BIT-PERFECT when it is so, TUBE PROCESSING while
// the tube is in the path, else what keeps it from being bit-perfect
// (DIGITAL VOL -6.0 dB and the like, as the output field says it).
export function badgeOf(s: PlaybackStatus | null): Badge {
  if (!s || s.state === "empty" || !s.path) return { text: "", kind: "none" };
  if (s.bitPerfect?.bitPerfect) return { text: "BIT-PERFECT", kind: "ok" };
  if (playingTubeType(s) !== null) return { text: "TUBE PROCESSING", kind: "tube" };
  const last = outputTags(s).at(-1);
  return last && last.kind === "warn" ? { text: last.text, kind: "warn" } : { text: "", kind: "none" };
}

export function ampState(l: ListeningSettings | null, s: PlaybackStatus | null, tubeAuto: boolean): AmpState {
  const mode = l ? (l.mode === "processed" ? "tube" : "pure") : s?.mode === "processed" ? "tube" : "pure";
  const playing = playingTubeType(s);
  const type = playing ?? l?.tubeType ?? "se300b";
  return {
    mode,
    tubesLit: playing !== null && s?.mode === "processed",
    look: lookOf(type),
    type,
    auto: tubeAuto,
    crossfeed: l?.crossfeedEnabled ?? false,
    badge: badgeOf(s),
    processingControls: mode === "tube",
  };
}

// The switch to TUBE also turns the tube on (the amp's TUBE is the tube):
// 音作りモード with the tube. PURE is 原音モード; the tube setting is kept.
export function modeChange(to: "pure" | "tube"): Partial<ListeningSettings> {
  return to === "tube" ? { mode: "processed", tubeEnabled: true } : { mode: "original" };
}
