// The output rate (task 10-4): 音作りモード only. The engine's rules
// (engine/include/ap/engine/output_rate.h); these are the texts. What is done
// is said, never that a higher rate sounds better (CLAUDE.md chapter 6).

import type { ListeningSettings } from "./settings";

export type OutputRateMode = "track" | "fixed" | "atleast" | "multiple";

export const OUTPUT_RATE_MODES: Record<OutputRateMode, string> = {
  track: "曲のまま",
  fixed: "固定",
  atleast: "下限",
  multiple: "整数倍",
};

// The rates offered for 固定 and 下限 (kOutputRateChoiceRates), and the multiples.
export const OUTPUT_RATE_CHOICES = [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000];
export const OUTPUT_RATE_MULTIPLES = [2, 4, 8];

// "44.1 kHz", "96 kHz".
export function rateText(hz: number): string {
  return `${Number((hz / 1000).toFixed(1))} kHz`;
}

// What the settings screen says the choice does.
export function outputRateNote(l: ListeningSettings): string {
  const r = rateText(l.outputRateHz);
  switch (l.outputRateMode) {
    case "fixed":
      return `どの曲も ${r} にして出します（それより高い曲は下げます）。`;
    case "atleast":
      return `${r} 未満の曲を ${r} に上げます。${r} 以上の曲はそのままです。`;
    case "multiple": {
      const k = l.outputRateMultiple;
      return `曲のレートの ${k} 倍で出します（44.1 → ${rateText(44100 * k).replace(" kHz", "")} kHz、48 → ${rateText(48000 * k)}）。DAC が受け付けないときは、受け付けるいちばん大きい倍数です。`;
    }
    default:
      return "曲のレートのまま出します（DAC が受け付けないレートの曲だけ、受け付けるレートへ変換します）。";
  }
}

// For the mode summary: "" for the default (the track's rate).
export function outputRateSummary(l: ListeningSettings): string {
  switch (l.outputRateMode) {
    case "fixed":
      return `出力レート（固定 ${rateText(l.outputRateHz)}）`;
    case "atleast":
      return `出力レート（${rateText(l.outputRateHz)} 以上）`;
    case "multiple":
      return `出力レート（曲の ${l.outputRateMultiple} 倍）`;
    default:
      return "";
  }
}

// The resampler stage's detail: "44100;96000;setting" or "...;unsupported".
export interface ResampleDetail {
  from: number;
  to: number;
  reason: "setting" | "unsupported";
}

export function parseResampleDetail(detail: string | undefined): ResampleDetail | null {
  if (!detail) return null;
  const [a, b, reason] = detail.split(";");
  const from = Number(a),
    to = Number(b);
  if (!(from > 0) || !(to > 0) || (reason !== "setting" && reason !== "unsupported")) return null;
  return { from, to, reason };
}

// "44.1 kHz → 96 kHz（設定）", "88.2 kHz → 44.1 kHz（DAC が 88.2 kHz を受け付けないため）".
export function resampleText(detail: string | undefined): string {
  const d = parseResampleDetail(detail);
  if (!d) return "ON";
  const why = d.reason === "setting" ? "設定" : `DAC が ${rateText(d.from)} を受け付けないため`;
  return `${rateText(d.from)} → ${rateText(d.to)}（${why}）`;
}
