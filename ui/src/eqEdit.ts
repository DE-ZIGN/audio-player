// The EQ editor's pure parts (task 6-3): knob scales, dragging, the graph's
// coordinates and which node is under the pointer.

import { dbText, frequencyText, type EqBand, type EqFilterType } from "./settings";
import { usesGain } from "./eqResponse";

export type Param = "frequency" | "gainDb" | "q" | "preamp";

interface ParamSpec {
  min: number;
  max: number;
  log: boolean;
}

// The knobs' ranges (typed values for the frequency may go wider, see LIMITS).
export const PARAMS: Record<Param, ParamSpec> = {
  frequency: { min: 20, max: 20000, log: true },
  gainDb: { min: -24, max: 24, log: false },
  q: { min: 0.1, max: 20, log: true },
  preamp: { min: -30, max: 0, log: false },
};

const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));

export function toNorm(p: Param, v: number): number {
  const s = PARAMS[p];
  const x = clamp(v, s.min, s.max);
  return s.log ? Math.log(x / s.min) / Math.log(s.max / s.min) : (x - s.min) / (s.max - s.min);
}

export function fromNorm(p: Param, n: number): number {
  const s = PARAMS[p];
  const t = clamp(n, 0, 1);
  return s.log ? s.min * Math.pow(s.max / s.min, t) : s.min + t * (s.max - s.min);
}

// To the step that is shown: 0.1 Hz below 100 Hz, 1 Hz below 10 kHz, then
// 10 Hz; 0.1 dB; Q to 0.01.
export function roundParam(p: Param, v: number): number {
  const r = (x: number, step: number) => Math.round(x / step) * step;
  let out: number;
  if (p === "frequency") out = v < 100 ? r(v, 0.1) : v < 10000 ? r(v, 1) : r(v, 10);
  else if (p === "q") out = r(v, 0.01);
  else out = r(v, 0.1);
  return Number(out.toFixed(2)) || 0; // no -0
}

export function formatParam(p: Param, v: number): string {
  if (p === "frequency") return frequencyText(v);
  if (p === "q") return v.toFixed(2);
  return dbText(v);
}

// Dragging a knob: 200 px of vertical movement turns it through its whole
// range (2000 px with fine control). Up turns it up.
export function dragKnob(p: Param, start: number, dyPixels: number, fine: boolean): number {
  const n = toNorm(p, start) - dyPixels / (fine ? 2000 : 200);
  return roundParam(p, fromNorm(p, n));
}

// Wheel notches in a wheel event (a notch is 100 in deltaY; one event may
// carry several; a touchpad's small deltas count as one).
export function wheelNotches(deltaY: number): number {
  return Math.min(10, Math.max(1, Math.round(Math.abs(deltaY) / 100)));
}

// One wheel notch: 1/100 of the range (1/1000 with fine control).
export function wheelKnob(p: Param, v: number, deltaY: number, fine: boolean): number {
  let out = v;
  for (let i = 0; i < wheelNotches(deltaY); i++) out = wheelStep(p, out, deltaY, fine);
  return out;
}

function wheelStep(p: Param, v: number, deltaY: number, fine: boolean): number {
  const step = (fine ? 0.001 : 0.01) * Math.sign(-deltaY);
  const next = roundParam(p, fromNorm(p, toNorm(p, v) + step));
  if (next !== v || step === 0) return next;
  // Rounding swallowed a small step: move by one shown step instead.
  const unit = p === "frequency" ? (v < 100 ? 0.1 : v < 10000 ? 1 : 10) : p === "q" ? 0.01 : 0.1;
  const s = PARAMS[p];
  return roundParam(p, clamp(v + Math.sign(step) * unit, s.min, s.max));
}

export function defaultQ(type: EqFilterType): number {
  return type === "peak" || type === "bandPass" || type === "notch" ? 1 : 0.71;
}

export function defaultValue(p: Param, band: EqBand | null): number {
  if (p === "frequency") return 1000;
  if (p === "q") return defaultQ(band?.type ?? "peak");
  return 0;
}

// --- the graph -----------------------------------------------------------------

export interface GraphFrame {
  width: number;
  height: number;
  minHz: number;
  maxHz: number;
  rangeDb: number; // -rangeDb .. +rangeDb
}

export function graphX(hz: number, f: GraphFrame): number {
  return (Math.log(hz / f.minHz) / Math.log(f.maxHz / f.minHz)) * f.width;
}

export function graphY(db: number, f: GraphFrame): number {
  return ((f.rangeDb - clamp(db, -f.rangeDb, f.rangeDb)) / (2 * f.rangeDb)) * f.height;
}

export function hzAt(x: number, f: GraphFrame): number {
  return f.minHz * Math.pow(f.maxHz / f.minHz, clamp(x / f.width, 0, 1));
}

export function dbAt(y: number, f: GraphFrame): number {
  return f.rangeDb - (clamp(y, 0, f.height) / f.height) * 2 * f.rangeDb;
}

// Where a band's node is drawn: at its gain, or on the 0 dB line for the
// types without one.
export function nodePoint(b: EqBand, f: GraphFrame): { x: number; y: number } {
  return { x: graphX(b.frequency, f), y: graphY(usesGain(b.type) ? b.gainDb : 0, f) };
}

// The nearest node within `radius` pixels, or -1.
export function nodeAt(bands: EqBand[], x: number, y: number, f: GraphFrame, radius = 12): number {
  let best = -1;
  let bestD = radius;
  bands.forEach((b, i) => {
    const p = nodePoint(b, f);
    const d = Math.hypot(p.x - x, p.y - y);
    if (d <= bestD) {
      best = i;
      bestD = d;
    }
  });
  return best;
}

// A new band where the graph was double-clicked: a peak at that frequency and gain.
export function bandAt(x: number, y: number, f: GraphFrame): EqBand {
  return {
    type: "peak",
    frequency: roundParam("frequency", hzAt(x, f)),
    gainDb: roundParam("gainDb", clamp(dbAt(y, f), PARAMS.gainDb.min, PARAMS.gainDb.max)),
    q: 1,
    enabled: true,
  };
}

// Dragging a node by (dx, dy) pixels from where it was: sideways is the
// frequency; up and down is the gain, or the Q for the types without a gain.
// Fine control moves a tenth as far.
export function dragNode(start: EqBand, dx: number, dy: number, f: GraphFrame, fine: boolean): EqBand {
  const k = fine ? 0.1 : 1;
  const frequency = roundParam("frequency", hzAt(graphX(start.frequency, f) + dx * k, f));
  if (usesGain(start.type)) {
    const gain = start.gainDb - ((dy * k) / f.height) * 2 * f.rangeDb;
    return { ...start, frequency, gainDb: roundParam("gainDb", clamp(gain, -f.rangeDb, f.rangeDb)) };
  }
  const q = start.q * Math.pow(2, (-dy * k) / 60);
  return { ...start, frequency, q: roundParam("q", clamp(q, PARAMS.q.min, PARAMS.q.max)) };
}

// A value typed into a knob: "2.5k", "2.5 kHz", "120 Hz", "−3 dB", "0,7"
// (the unit is optional). Within the limits, rounded to the shown step;
// null when it is not a number.
export function parseKnobText(p: Param, text: string): number | null {
  let t = text.trim().replace(/[，,]/g, ".").replace(/[−–]/g, "-").replace(/\s+/g, "");
  let scale = 1;
  const unit = /(k?hz|db|k)$/i.exec(t);
  if (unit) {
    t = t.slice(0, -unit[0].length);
    if (p === "frequency" && /^k/i.test(unit[0])) scale = 1000;
  }
  if (t === "") return null;
  const x = Number(t) * scale;
  if (!Number.isFinite(x)) return null;
  const limits = p === "frequency" ? { min: 10, max: 40000 } : PARAMS[p];
  return roundParam(p, clamp(x, limits.min, limits.max));
}
