// The tube's warm-up and the tubes' differences (task 9-8, ロマン枠): a show.
// What they do and the numbers are said; never that they sound better. The
// figures and the unit numbers are the engine's (engine/include/ap/engine/dsp/tube.h).

import type { TubeType } from "./settings";

export const TUBE_COLD_EMISSION = 0.5; // TubeStage::kColdEmission
export const WARMUP_TAU_SECONDS = 240; // TubeStage::kWarmUpTauSeconds
export const WARMUP_SECONDS = 1200; // TubeStage::kWarmUpSeconds
export const TUBE_UNIT_MIN = 1; // kMinTubeUnit
export const TUBE_UNIT_MAX = 99; // kMaxTubeUnit
export const EMISSION_TOLERANCE = 0.1; // kTubeEmissionTolerance
export const MU_TOLERANCE = 0.05; // kTubeMuTolerance

// The cold curve's share `seconds` after switch-on (TubeStage::coldShare).
export function coldShare(seconds: number): number {
  if (!(seconds > 0)) return 1;
  if (seconds >= WARMUP_SECONDS) return 0;
  const end = Math.exp(-WARMUP_SECONDS / WARMUP_TAU_SECONDS);
  return Math.min(1, Math.max(0, (Math.exp(-seconds / WARMUP_TAU_SECONDS) - end) / (1 - end)));
}

// --- the units (tubeUnitFor in the engine: the same arithmetic) ------------

const M64 = (1n << 64n) - 1n;

function splitMix(x: bigint): bigint {
  x = (x + 0x9e3779b97f4a7c15n) & M64;
  x = ((x ^ (x >> 30n)) * 0xbf58476d1ce4e5b9n) & M64;
  x = ((x ^ (x >> 27n)) * 0x94d049bb133111ebn) & M64;
  return x ^ (x >> 31n);
}

function spread(key: bigint): number {
  return Number(splitMix(key) >> 11n) * 2 ** -53 * 2 - 1;
}

const TYPE_INDEX: Record<TubeType, number> = { se300b: 0, ppel34: 1, ppclassa: 2 };

export function clampUnit(n: number): number {
  return Math.min(TUBE_UNIT_MAX, Math.max(TUBE_UNIT_MIN, Math.round(Number.isFinite(n) ? n : TUBE_UNIT_MIN)));
}

// Unit `n`'s tube in channel `ch` (0 left, 1 right); push-pull's second tube is `tube` 1.
export function tubeUnitFor(n: number, type: TubeType, ch: number, tube: number): { emission: number; mu: number } {
  const key = ((BigInt(clampUnit(n)) * 4n + BigInt(TYPE_INDEX[type])) * 2n + BigInt(ch & 1)) * 2n + BigInt(tube & 1);
  return { emission: 1 + EMISSION_TOLERANCE * spread(key * 2n), mu: 1 + MU_TOLERANCE * spread(key * 2n + 1n) };
}

const pct = (x: number) => {
  const v = (x - 1) * 100;
  return `${v > 0 ? "+" : v < 0 ? "−" : "±"}${Math.abs(v).toFixed(1)}%`;
};

// "左：エミッション +8.4%・μ −3.3%、右：…" (push-pull: the channel's two tubes joined by と).
export function unitChannelsText(n: number, type: TubeType): string {
  const tubes = type === "se300b" ? 1 : 2;
  return ["左", "右"]
    .map((side, ch) => {
      const parts: string[] = [];
      for (let t = 0; t < tubes; t++) {
        const u = tubeUnitFor(n, type, ch, t);
        parts.push(`エミッション ${pct(u.emission)}・μ ${pct(u.mu)}`);
      }
      return `${side}：${parts.join(" と ")}`;
    })
    .join("、");
}

export const unitLabel = (n: number) => `球 No.${n}`;

// --- the warm-up texts ---------------------------------------------------------------

export function warmUpText(seconds: number): string {
  const s = Math.max(0, seconds);
  if (s >= WARMUP_SECONDS) return `暖まった状態（起動から ${WARMUP_SECONDS / 60} 分を過ぎました）`;
  const left = Math.max(1, Math.ceil((WARMUP_SECONDS - s) / 60));
  return `ウォームアップ中：起動から ${Math.floor(s / 60)} 分（暖まりきるまで、あと約 ${left} 分）`;
}

// The amp view's tube glow (opacity): dimmer while cold.
export function glowOf(warmUpSeconds: number | null): number {
  return warmUpSeconds === null ? 1 : 0.55 + 0.45 * (1 - coldShare(warmUpSeconds));
}

// --- the tube stage's detail in the status ("ppel34;warmup=185;unit=12") ---------------

export interface TubeDetail {
  type: TubeType | null;
  warmUpSeconds: number | null; // the warm-up is on
  unit: number | null; // the units are on
}

const TYPES: TubeType[] = ["se300b", "ppel34", "ppclassa"];

export function parseTubeDetail(detail: string | undefined): TubeDetail {
  const out: TubeDetail = { type: null, warmUpSeconds: null, unit: null };
  if (!detail) return out;
  const [type, ...rest] = detail.split(";");
  if (!TYPES.includes(type as TubeType)) return out;
  out.type = type as TubeType;
  for (const part of rest) {
    const [k, v] = part.split("=");
    const x = Number(v);
    if (!Number.isFinite(x)) continue;
    if (k === "warmup") out.warmUpSeconds = x;
    if (k === "unit") out.unit = x;
  }
  return out;
}

// For the path display: ["ウォームアップ中（起動から 3 分）", "球 No.12"].
export function characterParts(warmUpSeconds: number | null, unit: number | null): string[] {
  const parts: string[] = [];
  if (warmUpSeconds !== null)
    parts.push(warmUpSeconds >= WARMUP_SECONDS ? "暖まった状態" : `ウォームアップ中（起動から ${Math.floor(Math.max(0, warmUpSeconds) / 60)} 分）`);
  if (unit !== null) parts.push(unitLabel(unit));
  return parts;
}
