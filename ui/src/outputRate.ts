// The output rate (task 10-4): 音作りモード only. The engine's rules
// (engine/include/ap/engine/output_rate.h); these are the texts. What is done
// is said, never that a higher rate sounds better (CLAUDE.md chapter 6).

import type { ListeningSettings } from "./settings";
import { t } from "./i18n";

export type OutputRateMode = "track" | "fixed" | "atleast" | "multiple";

// Each read gives the name in the language in use.
export const OUTPUT_RATE_MODES: Record<OutputRateMode, string> = {
  get track() {
    return t("status.rateMode.track");
  },
  get fixed() {
    return t("status.rateMode.fixed");
  },
  get atleast() {
    return t("status.rateMode.atleast");
  },
  get multiple() {
    return t("status.rateMode.multiple");
  },
};

// The rates offered for 固定 and 下限 (kOutputRateChoiceRates), and the multiples.
export const OUTPUT_RATE_CHOICES = [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000];
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
      return t("status.rateNote.fixed", { rate: r });
    case "atleast":
      return t("status.rateNote.atleast", { rate: r });
    case "multiple": {
      const k = l.outputRateMultiple;
      const number = (hz: number) => rateText(hz).replace(" kHz", "");
      return t("status.rateNote.multiple", { multiple: k, from44: number(44100 * k), from48: number(48000 * k) });
    }
    default:
      return t("status.rateNote.track");
  }
}

// For the mode summary: "" for the default (the track's rate).
export function outputRateSummary(l: ListeningSettings): string {
  switch (l.outputRateMode) {
    case "fixed":
      return t("status.rateSummary.fixed", { rate: rateText(l.outputRateHz) });
    case "atleast":
      return t("status.rateSummary.atleast", { rate: rateText(l.outputRateHz) });
    case "multiple":
      return t("status.rateSummary.multiple", { multiple: l.outputRateMultiple });
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
  const rates = { from: rateText(d.from), to: rateText(d.to) };
  return d.reason === "setting" ? t("status.resample.setting", rates) : t("status.resample.unsupported", rates);
}
