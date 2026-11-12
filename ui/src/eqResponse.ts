// The EQ's response computed on the page, so the curve follows a drag at once.
// The same biquad formulas (the Audio EQ Cookbook) as the engine's
// engine/src/dsp/eq.cpp; the app's self-test checks that both give the same
// curve and preamp. What is played is always the engine's.

import type { EqBand, EqFilterType } from "./settings";

const MAX_FREQUENCY_RATIO = 0.49;

export function usesGain(type: EqFilterType): boolean {
  return type === "peak" || type === "lowShelf" || type === "highShelf";
}

// A band that changes nothing: switched off, or a peak or shelf at 0 dB.
export function isNeutral(b: EqBand): boolean {
  return !b.enabled || (usesGain(b.type) && b.gainDb === 0);
}

// Normalized coefficients [b0, b1, b2, a1, a2]; null when it cannot be made at this rate.
export function biquad(b: EqBand, rate: number): number[] | null {
  if (!(b.frequency > 0) || b.frequency >= MAX_FREQUENCY_RATIO * rate || !(b.q > 0)) return null;
  const w0 = (2 * Math.PI * b.frequency) / rate;
  const cs = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * b.q);
  const a = Math.pow(10, b.gainDb / 40);
  const sa = 2 * Math.sqrt(a) * alpha;
  let c: number[];
  switch (b.type) {
    case "peak":
      c = [1 + alpha * a, -2 * cs, 1 - alpha * a, 1 + alpha / a, -2 * cs, 1 - alpha / a];
      break;
    case "lowShelf":
      c = [
        a * (a + 1 - (a - 1) * cs + sa),
        2 * a * (a - 1 - (a + 1) * cs),
        a * (a + 1 - (a - 1) * cs - sa),
        a + 1 + (a - 1) * cs + sa,
        -2 * (a - 1 + (a + 1) * cs),
        a + 1 + (a - 1) * cs - sa,
      ];
      break;
    case "highShelf":
      c = [
        a * (a + 1 + (a - 1) * cs + sa),
        -2 * a * (a - 1 + (a + 1) * cs),
        a * (a + 1 + (a - 1) * cs - sa),
        a + 1 - (a - 1) * cs + sa,
        2 * (a - 1 - (a + 1) * cs),
        a + 1 - (a - 1) * cs - sa,
      ];
      break;
    case "lowPass":
      c = [(1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + alpha, -2 * cs, 1 - alpha];
      break;
    case "highPass":
      c = [(1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + alpha, -2 * cs, 1 - alpha];
      break;
    case "bandPass":
      c = [alpha, 0, -alpha, 1 + alpha, -2 * cs, 1 - alpha];
      break;
    case "notch":
      c = [1, -2 * cs, 1, 1 + alpha, -2 * cs, 1 - alpha];
      break;
  }
  const a0 = c[3];
  return [c[0] / a0, c[1] / a0, c[2] / a0, c[4] / a0, c[5] / a0];
}

function magnitudeSquared(c: number[], hz: number, rate: number): number {
  const w = (2 * Math.PI * hz) / rate;
  const [b0, b1, b2, a1, a2] = c;
  const cos1 = Math.cos(w), cos2 = Math.cos(2 * w), sin1 = Math.sin(w), sin2 = Math.sin(2 * w);
  const nr = b0 + b1 * cos1 + b2 * cos2, ni = -(b1 * sin1 + b2 * sin2);
  const dr = 1 + a1 * cos1 + a2 * cos2, di = -(a1 * sin1 + a2 * sin2);
  return (nr * nr + ni * ni) / (dr * dr + di * di);
}

// The coefficients of the bands that do something at this rate.
function sections(bands: EqBand[], rate: number): number[][] {
  const out: number[][] = [];
  for (const b of bands) {
    if (isNeutral(b)) continue;
    const c = biquad(b, rate);
    if (c) out.push(c);
  }
  return out;
}

function sectionsDb(cs: number[][], hz: number, rate: number): number {
  let m2 = 1;
  for (const c of cs) m2 *= magnitudeSquared(c, hz, rate);
  return 10 * Math.log10(m2);
}

// The bands' combined response at `hz` in dB (without the preamp).
export function responseDb(bands: EqBand[], hz: number, rate: number): number {
  return sectionsDb(sections(bands, rate), hz, rate);
}

// The automatic preamp: minus the largest boost from 20 Hz to 0.49 of the rate.
export function autoPreampDb(bands: EqBand[], rate: number): number {
  const cs = sections(bands, rate);
  if (cs.length === 0) return 0;
  const lo = 20, hi = MAX_FREQUENCY_RATIO * rate;
  let max = 0;
  for (let i = 0; i <= 2000; i++) max = Math.max(max, sectionsDb(cs, lo * Math.pow(hi / lo, i / 2000), rate));
  for (const b of bands)
    if (!isNeutral(b) && b.frequency >= lo && b.frequency <= hi) max = Math.max(max, sectionsDb(cs, b.frequency, rate));
  return max > 0 ? -max : 0;
}

// Enabled bands that cannot be made at this rate.
export function skippedBands(bands: EqBand[], rate: number): number {
  return bands.filter((b) => b.enabled && biquad(b, rate) === null).length;
}

// [Hz, dB] from 20 Hz to 20 kHz (or just below Nyquist), evenly on a log scale.
export function responsePoints(bands: EqBand[], rate: number, count = 160): [number, number][] {
  const cs = sections(bands, rate);
  const top = Math.min(20000, MAX_FREQUENCY_RATIO * rate);
  const points: [number, number][] = [];
  for (let i = 0; i < count; i++) {
    const hz = 20 * Math.pow(top / 20, i / (count - 1));
    points.push([hz, sectionsDb(cs, hz, rate)]);
  }
  return points;
}
